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
                                <tr :style="{ height: `${entitySizeConfig.headerHeight}px` }">
                                    <th
                                        class="text-center font-weight-bold text-no-wrap rounded-tr-lg rounded-tl-lg px-4"
                                        colspan="2"
                                        :style="{
                                            borderTop: `8px solid ${getNodeHighlightColor(node)}`,
                                            borderRight: getBorderStyle(node),
                                            borderLeft: getBorderStyle(node),
                                        }"
                                    >
                                        {{ $helpers.unquoteIdentifier(node.data.name) }}
                                    </th>
                                </tr>
                            </thead>
                            <tbody>
                                <tr
                                    v-for="col in node.data.definitions.cols"
                                    :key="`key_${node.id}_${col.name}`"
                                    :style="{
                                        height: `${entitySizeConfig.rowHeight}px`,
                                        ...getHighlightColStyle({ node, colName: col.name }),
                                    }"
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
                                                <erd-key-icon
                                                    :data="getKeyIcon({ node, colName: col.name })"
                                                />
                                            </div>
                                            {{ $helpers.unquoteIdentifier(col.name) }}
                                        </div>
                                    </td>
                                    <td
                                        class="px-2 text-end"
                                        :style="{
                                            borderRight: getBorderStyle(node),
                                            ...getCellStyle({ node, colName: col.name }),
                                            color:
                                                $typy(
                                                    getHighlightColStyle({
                                                        node,
                                                        colName: col.name,
                                                    }),
                                                    'color'
                                                ).safeString || '#6c7c7b',
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
import ErdKeyIcon from '@share/components/common/MxsSvgGraphs/ErdKeyIcon'
import { EVENT_TYPES } from '@share/components/common/MxsSvgGraphs/config'
import tokens from '@wsSrc/utils/createTableTokens'

export default {
    name: 'mxs-erd',
    components: {
        'graph-board': GraphBoard,
        'graph-nodes': GraphNodes,
        'erd-key-icon': ErdKeyIcon,
    },
    props: {
        ctrDim: { type: Object, required: true },
        data: { type: Object, required: true },
        graphConfigData: { type: Object, required: true },
    },
    data() {
        return {
            isRendering: false,
            areSizesCalculated: false,
            svgGroup: null,
            graphNodeCoordMap: {},
            stagingData: null,
            graphNodes: [],
            graphLinks: [],
            simulation: null,
            defNodeSize: { width: 200, height: 100 },
            chosenLinks: [],
            entityLink: null,
            graphConfig: null,
            isDraggingNode: false,
        }
    },
    computed: {
        nodeKeyMap() {
            return this.graphNodes.reduce((map, node) => {
                map[node.id] = node.data.definitions.keys
                return map
            }, {})
        },
        entitySizeConfig() {
            return this.graphConfigData.linkShape.entitySizeConfig
        },
        highlightColStyleMap() {
            return this.chosenLinks.reduce((map, link) => {
                const {
                    source,
                    target,
                    relationshipData: { source_attr, target_attr },
                    styles: { invisibleHighlightColor },
                } = link

                if (!map[source.id]) map[source.id] = []
                if (!map[target.id]) map[target.id] = []
                const style = {
                    backgroundColor: invisibleHighlightColor,
                    color: 'white',
                }
                map[source.id].push({ col: source_attr, ...style })
                map[target.id].push({ col: target_attr, ...style })
                return map
            }, {})
        },
        isAttrToAttr() {
            return this.$typy(this.graphConfigData, 'link.isAttrToAttr').safeBoolean
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
        this.assignData()
    },
    beforeDestroy() {
        this.$typy(this.unwatch_graphConfigData).safeFunction()
    },
    methods: {
        handleFilterCompositeKeys(v) {
            if (v) this.graphLinks = this.stagingData.links
            else this.graphLinks = this.graphLinks.filter(link => !link.isPartOfCompositeKey)
        },
        /**
         * D3 mutates data, this method deep clones data to make the original is intact
         * @param {Object} data
         */
        assignData() {
            this.stagingData = this.$helpers.lodash.cloneDeep(this.data)
            this.graphNodes = this.stagingData.nodes
            this.graphLinks = this.stagingData.links
            this.handleFilterCompositeKeys(this.isAttrToAttr)
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
            this.graphConfig = new GraphConfig(this.graphConfigData)
            this.entityLink = new EntityLink(this.graphConfig)
            this.watchConfig()
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
        getNodeHighlightColor: node => node.styles.highlightColor,
        getBorderStyle(node) {
            const style = `1px solid ${this.getNodeHighlightColor(node)}`
            return style
        },
        isLastCol: ({ node, colName }) => colName === node.data.definitions.cols.at(-1).name,
        isFirstCol: ({ node, colName }) => colName === node.data.definitions.cols.at(0).name,
        getCellStyle({ node, colName }) {
            return {
                borderTop: this.isFirstCol({ node, colName }) ? this.getBorderStyle(node) : 'none',
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
        setEventLinkStyles(eventType) {
            this.entityLink.setEventStyles({ links: this.chosenLinks, eventType })
            this.drawLinks()
        },
        onNodeDrag({ node, diffX, diffY }) {
            const nodeData = this.graphNodes.find(n => n.id === node.id)
            nodeData.x = nodeData.x + diffX
            nodeData.y = nodeData.y + diffY
            this.setChosenLinks(node)
            if (!this.isDraggingNode) this.setEventLinkStyles(EVENT_TYPES.DRAGGING)
            this.isDraggingNode = true
            /**
             * drawLinks is called inside setEventLinkStyles method but it run once.
             * To ensure that the paths of links continue to be redrawn, call it again while
             * dragging the node
             */
            this.drawLinks()
        },
        onNodeDragEnd() {
            if (this.isDraggingNode) {
                this.setEventLinkStyles(EVENT_TYPES.NONE)
                this.isDraggingNode = false
                this.chosenLinks = []
            }
        },
        mouseenterNode({ node }) {
            this.setChosenLinks(node)
            this.setEventLinkStyles(EVENT_TYPES.HOVER)
        },
        mouseleaveNode() {
            this.setEventLinkStyles(EVENT_TYPES.NONE)
            this.chosenLinks = []
        },
        findKeyTypeByColName({ node, colName }) {
            const nodeKeys = this.nodeKeyMap[node.id]
            const keyTypes = [tokens.primaryKey, tokens.uniqueKey, tokens.key]
            return keyTypes.find(type =>
                this.$typy(nodeKeys, `[${type}]`).safeArray.some(key =>
                    key.index_col_names.some(item => item.name === colName)
                )
            )
        },
        getKeyIcon({ node, colName }) {
            const keyType = this.findKeyTypeByColName({ node, colName })
            const { color } = this.getHighlightColStyle({ node, colName }) || {}
            switch (keyType) {
                case tokens.primaryKey:
                    return {
                        icon: 'mdi-key-variant',
                        color: color ? color : 'primary',
                        style: {
                            transform: 'rotate(180deg) scale(1, -1)',
                        },
                        size: 18,
                    }
                case tokens.uniqueKey:
                    return {
                        icon: '$vuetify.icons.mxs_uniqueIndexKey',
                        color: color ? color : 'navigation',
                        size: 16,
                    }
                case tokens.key:
                    return {
                        icon: '$vuetify.icons.mxs_indexKey',
                        color: color ? color : 'navigation',
                        size: 16,
                    }
            }
        },
        getHighlightColStyle({ node, colName }) {
            const cols = this.highlightColStyleMap[node.id] || []
            return cols.find(item => item.col === colName)
        },
        watchConfig() {
            this.unwatch_graphConfigData = this.$watch(
                'graphConfigData',
                (v, oV) => {
                    /**
                     * Because only one attribute can be changed at a time, so it's safe to
                     * access the diff with hard-code indexes.
                     */
                    const diff = this.$typy(this.$helpers.deepDiff(oV, v), '[0]').safeObjectOrEmpty
                    const diffKey = diff.path[1]
                    const aValueDiff = this.$helpers.lodash.objGet(v, diff.path.join('.'))
                    this.graphConfig.updateConfig({
                        key: diff.path[0],
                        patch: { [diffKey]: aValueDiff },
                    })
                    switch (diffKey) {
                        case 'isAttrToAttr':
                            this.handleIsAttrToAttrMode(aValueDiff)
                            break
                    }
                    this.drawLinks()
                },
                { deep: true }
            )
        },
        /**
         * If value is true, the diagram shows all links including composite links for composite keys
         * @param {boolean} v
         */
        handleIsAttrToAttrMode(v) {
            this.handleFilterCompositeKeys(v)
            this.simulation.force('link').links(this.graphLinks)
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
