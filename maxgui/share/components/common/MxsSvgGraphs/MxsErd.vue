<template>
    <div class="fill-height er-diagram">
        <div v-if="isDraggingNode" class="dragging-mask" />
        <v-progress-linear v-if="isRendering" indeterminate color="primary" />
        <!-- Graph-board and its child components will be rendered but they won't be visible until
             the simulation is done. This ensures node size can be calculated dynamically.
        -->
        <graph-board
            :style="{ visibility: isRendering ? 'hidden' : 'visible' }"
            :dim="ctrDim"
            :graphDim="ctrDim"
            @get-graph-ctr="svgGroup = $event"
        >
            <template v-slot:append="{ data: { transform, zoom } }">
                <graph-nodes
                    ref="graphNodes"
                    :nodes="graphNodes"
                    :coordMap.sync="graphNodeCoordMap"
                    :style="{ transform }"
                    :defNodeSize="defNodeSize"
                    draggable
                    hoverable
                    :boardZoom="zoom"
                    dynHeight
                    dynWidth
                    @node-size-map="setNodeSize"
                    @drag="onNodeDrag"
                    @drag-end="onNodeDragEnd"
                    @mouseenter="mouseenterNode"
                    @mouseleave="mouseleaveNode"
                >
                    <template v-slot:default="{ data: { node } }">
                        <table class="entity-table">
                            <thead>
                                <tr :style="{ height: `${entityHeaderHeight}px` }">
                                    <th
                                        class="px-4 rounded-tr-lg rounded-tl-lg py-1 text-center font-weight-bold text-no-wrap"
                                        :style="{
                                            backgroundColor: node.styles.highlightColor,
                                            color: node.styles.headerTxtColor,
                                        }"
                                        colspan="2"
                                    >
                                        {{ $helpers.unquoteIdentifier(node.data.name) }}
                                    </th>
                                </tr>
                            </thead>
                            <tbody>
                                <tr
                                    v-for="col in node.data.definitions.cols"
                                    :key="`key_${node.id}_${col.name}`"
                                    :style="{ height: `${entitySizeConfig.rowHeight}px` }"
                                >
                                    <td
                                        class="pl-1 pr-2"
                                        :style="{
                                            borderLeft: getBorderStyle(node),
                                            ...getCellStyle({ node, colName: col.name }),
                                        }"
                                    >
                                        <div class="d-flex align-center">
                                            <div class="key-icon-ctr">
                                                <!-- TODO: Add key icons for PK, Index, Unique Index, FK -->
                                            </div>
                                            {{ $helpers.unquoteIdentifier(col.name) }}
                                        </div>
                                    </td>
                                    <td
                                        class="px-2 text-end mxs-color-helper text-small-text"
                                        :style="{
                                            borderRight: getBorderStyle(node),
                                            ...getCellStyle({ node, colName: col.name }),
                                        }"
                                    >
                                        {{ col.data_type }}
                                        <template v-if="$typy(col, 'data_type_size').isDefined">
                                            ({{ col.data_type_size }})
                                        </template>
                                    </td>
                                </tr>
                            </tbody>
                        </table>
                    </template>
                </graph-nodes>
            </template>
        </graph-board>
    </div>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import {
    forceSimulation,
    forceLink,
    forceManyBody,
    forceCenter,
    forceCollide,
    forceX,
    forceY,
} from 'd3-force'
import GraphBoard from '@share/components/common/MxsSvgGraphs/GraphBoard.vue'
import GraphNodes from '@share/components/common/MxsSvgGraphs/GraphNodes.vue'
import GraphConfig from '@share/components/common/MxsSvgGraphs/GraphConfig'
import EntityLink from '@share/components/common/MxsSvgGraphs/EntityLink'
import { LINK_SHAPES, EVENT_TYPES } from '@share/components/common/MxsSvgGraphs/config'

export default {
    name: 'mxs-erd',
    components: {
        'graph-board': GraphBoard,
        'graph-nodes': GraphNodes,
    },
    props: {
        ctrDim: { type: Object, required: true },
        data: { type: Object, required: true },
    },
    data() {
        return {
            isRendering: false,
            areSizesCalculated: false,
            svgGroup: null,
            graphNodeCoordMap: {},
            graphNodes: [],
            graphLinks: [],
            simulation: null,
            defNodeSize: { width: 200, height: 100 },
            chosenLinks: [],
            entityHeaderHeight: 32,
            entityLink: null,
            graphConfig: null,
            strokeWidth: 1,
            isDraggingNode: false,
            //TODO: Add inputs to change below values
            isAttrToAttr: false,
            linkShapeType: LINK_SHAPES.ORTHO,
        }
    },
    computed: {
        entitySizeConfig() {
            return { rowHeight: 32, rowOffset: 4, nodeOffsetHeight: this.entityHeaderHeight }
        },
        erdGraphConfig() {
            return {
                link: {
                    color: '#424f62',
                    strokeWidth: this.strokeWidth,
                    opacity: 1,
                    dashArr: '5',
                    isAttrToAttr: this.isAttrToAttr,
                },
                marker: { width: 18 },
                linkShape: {
                    type: this.linkShapeType,
                    entitySizeConfig: this.entitySizeConfig,
                },
            }
        },
    },
    watch: {
        // wait until graph-nodes sizes are calculated
        areSizesCalculated(v) {
            if (v) {
                this.runSimulation()
            }
        },
    },
    created() {
        this.isRendering = true
        this.assignData(this.data)
    },
    methods: {
        /**
         * D3 mutates data, this method deep clones data to make the original is intact
         * @param {Object} data
         */
        assignData(data) {
            const cloned = this.$helpers.lodash.cloneDeep(data)
            this.graphNodes = cloned.nodes
            this.graphLinks = cloned.links
        },
        setNodeSize(map) {
            this.graphNodes = this.graphNodes.map(node => ({ ...node, size: map[node.id] }))
            this.areSizesCalculated = Boolean(Object.keys(map).length)
        },
        runSimulation() {
            if (this.graphNodes.length) {
                this.simulation = forceSimulation(this.graphNodes)
                    .force(
                        'link',
                        forceLink(this.graphLinks).id(d => d.id)
                    )
                    .force(
                        'charge',
                        forceManyBody()
                            .strength(-15)
                            .theta(0.5)
                    )
                    .force('center', forceCenter(this.ctrDim.width / 2, this.ctrDim.height / 2))
                    .force('x', forceX().strength(0.1))
                    .force('y', forceY().strength(0.1))
                    .alphaMin(0.1)
                    .on('end', () => {
                        this.drawChart()
                        this.isRendering = false
                    })
                this.handleCollision()
            }
        },
        initLinkInstance() {
            this.graphConfig = new GraphConfig(this.erdGraphConfig)
            this.entityLink = new EntityLink(this.graphConfig)
        },
        drawChart() {
            this.setGraphNodeCoordMap()
            this.initLinkInstance()
            this.drawLinks()
        },
        setGraphNodeCoordMap() {
            this.graphNodeCoordMap = this.graphNodes.reduce((map, n) => {
                const { x, y, id } = n
                if (id) map[id] = { x, y: y }
                return map
            }, {})
        },
        getLinks() {
            return this.simulation.force('link').links()
        },
        drawLinks() {
            this.entityLink.draw({ containerEle: this.svgGroup, data: this.getLinks() })
        },
        handleCollision() {
            this.simulation.force(
                'collide',
                forceCollide().radius(d => {
                    const { width, height } = d.size
                    // Because nodes are densely packed,  this adds an extra radius of 100 pixels to the nodes
                    return Math.sqrt(width * width + height * height) / 2 + 100
                })
            )
        },
        getBorderStyle(node) {
            const { highlightColor } = node.styles
            const style = `1px solid ${highlightColor}`
            return style
        },
        isLastCol: ({ node, colName }) => colName === node.data.definitions.cols.at(-1).name,
        getCellStyle({ node, colName }) {
            return {
                borderBottom: this.isLastCol({ node, colName })
                    ? this.getBorderStyle(node)
                    : 'none',
            }
        },
        setChosenLinks(node) {
            this.chosenLinks = this.getLinks().filter(
                d => d.source.id === node.id || d.target.id === node.id
            )
        },
        /**
         * TODO: Change the color of links and highlight FK columns
         */
        tmpUpdateChosenLinksStyle(eventType) {
            this.entityLink.tmpUpdateLinksStyle({
                links: this.chosenLinks,
                eventType: eventType,
            })
            this.drawLinks()
        },
        onNodeDrag({ node, diffX, diffY }) {
            const nodeData = this.graphNodes.find(n => n.id === node.id)
            nodeData.x = nodeData.x + diffX
            nodeData.y = nodeData.y + diffY
            this.setChosenLinks(node)
            if (!this.isDraggingNode) this.tmpUpdateChosenLinksStyle(EVENT_TYPES.DRAGGING)
            this.isDraggingNode = true
            /**
             * drawLinks is called inside tmpUpdateChosenLinksStyle method but it run once.
             * To ensure that the paths of links continue to be redrawn, call it again while
             * dragging the node
             */
            this.drawLinks()
        },
        onNodeDragEnd() {
            if (this.isDraggingNode) this.tmpUpdateChosenLinksStyle(EVENT_TYPES.NONE)
            this.isDraggingNode = false
        },
        mouseenterNode({ node }) {
            this.setChosenLinks(node)
            this.tmpUpdateChosenLinksStyle(EVENT_TYPES.HOVER)
        },
        mouseleaveNode() {
            this.tmpUpdateChosenLinksStyle(EVENT_TYPES.NONE)
        },
    },
}
</script>

<style lang="scss" scoped>
.entity-table {
    background: white;
    width: 100%;
    border-spacing: 0px;
    tbody {
        tr {
            &:hover {
                background: $tr-hovered-color;
            }
            td {
                white-space: nowrap;
                .key-icon-ctr {
                    width: 20px;
                }
            }
            &:last-of-type {
                td:first-of-type {
                    border-bottom-left-radius: 8px !important;
                }
                td:last-of-type {
                    border-bottom-right-radius: 8px !important;
                }
            }
        }
    }
}
</style>
