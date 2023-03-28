<template>
    <div class="fill-height er-diagram">
        <v-progress-linear v-if="isRendering" indeterminate color="primary" />
        <graph-board :dim="ctrDim" :graphDim="ctrDim" @get-graph-ctr="svgGroup = $event">
            <template v-slot:append="{ data: { transform, zoom } }">
                <graph-nodes
                    ref="graphNodes"
                    :nodes="graphNodes"
                    :coordMap.sync="graphNodeCoordMap"
                    :style="{ transform }"
                    :defNodeSize="defNodeSize"
                    draggable
                    :boardZoom="zoom"
                    dynHeight
                    dynWidth
                    @node-size-map="dynNodeSizeMap = $event"
                    @drag="onNodeDrag"
                    @drag-end="onNodeDragEnd"
                >
                    <template v-slot:default="{ data: { node } }">
                        <div class="entity">
                            <div
                                class="entity-header px-4 rounded-tr-lg rounded-tl-lg py-1 text-center font-weight-bold"
                                :style="{
                                    backgroundColor: node.data.highlightColor,
                                    height: `${entityHeaderHeight}px`,
                                }"
                            >
                                {{ node.id }}
                            </div>
                            <table
                                class="entity-fields px-2 rounded-br-lg rounded-bl-lg"
                                :style="{ border: `1px solid ${node.data.highlightColor}` }"
                            >
                                <tr
                                    v-for="col in node.data.cols"
                                    :key="`key_${node.id}_${col.name}`"
                                    :style="{ height: `${trHeight}px` }"
                                >
                                    <td>{{ col.name }}</td>
                                </tr>
                            </table>
                        </div>
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
import { line, curveLinear } from 'd3-shape'
import {
    forceSimulation,
    forceLink,
    forceManyBody,
    forceCenter,
    forceCollide,
    forceX,
    forceY,
} from 'd3-force'
import GraphBoard from '@share/components/common/MxsCharts/GraphBoard.vue'
import GraphNodes from '@share/components/common/MxsCharts/GraphNodes.vue'
import { drawLinks, changeLinkGroupStyle } from '@share/components/common/MxsCharts/utils'

export default {
    name: 'er-diagram',
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
            svgGroup: null,
            graphNodeCoordMap: {},
            graphData: { nodes: [], links: [] },
            graphNodes: [], // staging data
            simulation: null,
            defNodeSize: { width: 200, height: 100 },
            dynNodeSizeMap: {},
            isDraggingNode: false,
            highlightedLinks: [],
            trHeight: 32,
            entityHeaderHeight: 32,
        }
    },

    watch: {
        isDraggingNode(v) {
            for (const link of this.highlightedLinks) changeLinkGroupStyle({ link, isDragging: v })
        },
        data: {
            deep: true,
            immediate: true,
            handler(v, oV) {
                if (!this.$helpers.lodash.isEqual(v, oV)) {
                    this.isRendering = true
                    this.assignData(v)
                    this.drawChart()
                }
            },
        },
    },
    methods: {
        /**
         * D3 mutates data, this method deep clones data to make the original is intact
         * @param {Object} data
         */
        assignData(data) {
            const cloned = this.$helpers.lodash.cloneDeep(data)
            this.graphData = cloned
        },
        drawGraphNodes() {
            this.graphNodes = this.graphData.nodes
        },
        drawChart() {
            this.drawGraphNodes()
            this.$nextTick(() => {
                if (this.graphData.nodes.length) {
                    const { nodes, links } = this.graphData
                    this.simulation = forceSimulation(nodes)
                        .force(
                            'link',
                            forceLink(links).id(d => d.id)
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
                            this.render()
                            this.isRendering = false
                        })
                    this.handleCollision()
                }
            })
        },
        render() {
            this.setGraphNodeCoordMap()
            this.handleDrawLinks()
        },
        setGraphNodeCoordMap() {
            this.graphNodeCoordMap = this.graphData.nodes.reduce((map, n) => {
                const { x, y, id } = n
                if (id) map[id] = { x, y }
                return map
            }, {})
        },
        /**
         * Reposition the y value to the center of the relational column
         * @param {Object} param.node - entity node
         * @param {String} param.col - name of the relational column
         */
        repositionY({ node, col }) {
            const size = this.dynNodeSizeMap[node.id]
            const colIdx = node.data.cols.findIndex(c => c.name === col)
            return (
                node.y +
                size.height / 2 -
                (size.height - this.entityHeaderHeight) +
                colIdx * this.trHeight +
                this.trHeight / 2
            )
        },
        /**
         * Reposition the x value to either right or left edge of the relational column
         * @param {Object} param.source - source node
         * @param {Object} param.target - target node
         */
        handleRepositionX({ source, target }) {
            const sourceSize = this.dynNodeSizeMap[source.id]
            const targetSize = this.dynNodeSizeMap[target.id]
            const isSrcRightWard = source.x - target.x > 0
            const srcOffset = sourceSize.width / 2,
                targetOffset = targetSize.width / 2
            return {
                sourceX: source.x + (isSrcRightWard ? -srcOffset : srcOffset),
                targetX: target.x + (isSrcRightWard ? targetOffset : -targetOffset),
            }
        },
        //TODO: Replace curveLinear with a custom function
        linkPathGenerator(d) {
            const {
                source,
                target,
                relationshipData: { source_col, target_col },
            } = d
            const { sourceX, targetX } = this.handleRepositionX({ source, target })
            let sourceY = this.repositionY({ node: source, col: source_col }),
                targetY = this.repositionY({ node: target, col: target_col })
            return line()
                .x(d => d.x)
                .y(d => d.y)
                .curve(curveLinear)([
                { x: sourceX, y: sourceY },
                { x: targetX, y: targetY },
            ])
        },
        /**
         * @param {Object} node -  node
         * @returns {Object} - { width: Number, height: Number}
         */
        getNodeSize(node) {
            return this.$refs.graphNodes.getNodeSize(this.$typy(node, 'id').safeString)
        },
        linkStrokeGenerator: d => d.source.data.linkColor,
        handleDrawLinks() {
            drawLinks({
                containerEle: this.svgGroup,
                data: this.simulation.force('link').links(),
                linkPathGenerator: this.linkPathGenerator,
                linkStrokeGenerator: this.linkStrokeGenerator,
            })
        },
        handleCollision() {
            this.simulation.force(
                'collide',
                forceCollide().radius(d => {
                    const { width, height } = this.getNodeSize(d)
                    // Because nodes are densely packed,  this adds an extra radius of 25 pixels to the nodes
                    return Math.sqrt(width * width + height * height) / 2 + 25
                })
            )
        },
        onNodeDrag({ node, diffX, diffY }) {
            this.isDraggingNode = true
            const nodeData = this.graphData.nodes.find(n => n.id === node.id)
            nodeData.x = nodeData.x + diffX
            nodeData.y = nodeData.y + diffY
            const links = this.simulation.force('link').links()
            this.highlightedLinks = links.filter(
                d => d.source.id === node.id || d.target.id === node.id
            )
            this.handleDrawLinks()
        },
        onNodeDragEnd() {
            this.isDraggingNode = false
        },
    },
}
</script>

<style lang="scss" scoped>
.entity {
    background: rgba(255, 255, 255, 0);
    .entity-fields {
        width: 100%;
        border-spacing: 0px;
    }
}
</style>
