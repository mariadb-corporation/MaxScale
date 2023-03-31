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
                                    color: node.data.headerTxtColor,
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
    computed: {
        // Ensure that the marker remains visible while dragging a node by allocating a specific width.
        relMarkFixedWidth() {
            return 30
        },
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
         * Return the y position of a node based on its dynamic height and
         * the provided column name
         * @param {Object} param.node - The node to reposition.
         * @param {string} param.col - The name of the relational column
         * @returns {number} The new y position of the node.
         */
        getColYPos({ node, col }) {
            const nodeHeight = this.dynNodeSizeMap[node.id].height
            const colIdx = node.data.cols.findIndex(c => c.name === col)
            return (
                node.y +
                nodeHeight / 2 -
                (nodeHeight - this.entityHeaderHeight) +
                colIdx * this.trHeight +
                this.trHeight / 2
            )
        },
        /**
         * Get the y position of source and target nodes
         * @param {Object} param.source - The source node of the relationship.
         * @param {Object} param.target - The target node of the relationship.
         * @param {string} param.relationshipData.source_col - The name of the relational column of the source
         * @param {string} param.relationshipData.target_col - The name of the relational column of the target
         * @returns {Object} An object containing the new y positions of the source and target nodes.
         */
        getYPositions({ source, target, relationshipData }) {
            const { source_col, target_col } = relationshipData
            return {
                y0: this.getColYPos({ node: source, col: source_col }),
                y1: this.getColYPos({ node: target, col: target_col }),
            }
        },
        /**
         * Checks the horizontal position of the target node.
         * @param {number} params.srcX - The horizontal position of the source node relative to the center.
         * @param {number} params.targetX - The horizontal position of the target node relative to the center.
         * @param {number} params.halfSrcWidth - Half the width of the source node.
         * @param {number} params.halfTargetWidth - Half the width of the target node.
         * @returns {string} Returns the relative position of the target node to the source node.
         * "target-right" if the target node is to the right of the source node.
         * "target-left" if the target node is to the left of the source node.
         * "intersect" if the target node intersects with the source node.
         */
        checkTargetPosX({ srcX, targetX, halfSrcWidth, halfTargetWidth }) {
            const srcZone = [srcX - halfSrcWidth, srcX + halfSrcWidth],
                targetZone = [targetX - halfTargetWidth, targetX + halfTargetWidth]
            if (targetZone[0] - srcZone[1] >= 0) return 'target-right'
            else if (srcZone[0] - targetZone[1] >= 0) return 'target-left'
            return 'intersect'
        },
        /**
         * Repositions the x position of a node and its target based on the
         * dynamic node sizes and the distance between them.
         * @param {Object} param.source - The source node.
         * @param {Object} param.target - The target node.
         * @returns {Object} An object containing the x positions of the source and target
         * nodes, as well as the midpoint positions of the link.
         */
        getXPositions({ source, target }) {
            const srcWidth = this.dynNodeSizeMap[source.id].width,
                targetWidth = this.dynNodeSizeMap[target.id].width,
                halfSrcWidth = srcWidth / 2,
                halfTargetWidth = targetWidth / 2

            // D3 returns the mid point of the entities for source.x, target.x
            const srcX = source.x,
                targetX = target.x

            let x0 = srcX,
                x1 = targetX,
                dx1 = x0,
                dx4 = x1,
                dx2,
                dx3

            switch (this.checkTargetPosX({ srcX, targetX, halfSrcWidth, halfTargetWidth })) {
                case 'target-right': {
                    x0 = srcX + halfSrcWidth
                    x1 = targetX - halfTargetWidth
                    dx1 = x0 + this.relMarkFixedWidth
                    dx4 = x1 - this.relMarkFixedWidth
                    if (dx4 - dx1 <= 0) {
                        dx2 = dx1
                        dx3 = dx4
                    }
                    break
                }
                case 'target-left': {
                    x0 = srcX - halfSrcWidth
                    x1 = targetX + halfTargetWidth
                    dx1 = x0 - this.relMarkFixedWidth
                    dx4 = x1 + this.relMarkFixedWidth
                    if (dx1 - dx4 <= 0) {
                        dx2 = dx1
                        dx3 = dx4
                    }
                    break
                }
                case 'intersect': {
                    // move x0 & x1 to the right edge of the nodes
                    x0 = srcX + halfSrcWidth
                    x1 = targetX + halfTargetWidth
                    dx1 = x1 + this.relMarkFixedWidth
                    dx4 = dx1
                    if (dx1 - this.relMarkFixedWidth <= x0) {
                        dx1 = x0 + this.relMarkFixedWidth
                        if (dx4 - dx1 <= 0) {
                            dx2 = dx1
                            dx3 = dx1
                        }
                    }
                    if (dx4 <= dx1) dx4 = dx1
                    break
                }
            }

            return { x0, x1, dx1, dx2, dx3, dx4 }
        },
        //TODO: handle overlapped y pos as a field can be associated with multiple tables
        linkPathGenerator(d) {
            const { source, target, relationshipData } = d
            const { y0, y1 } = this.getYPositions({ source, target, relationshipData })
            const { x0, x1, dx1, dx2, dx3, dx4 } = this.getXPositions({
                source,
                target,
            })
            const point0 = [x0, y0]
            const point1 = [dx1, y0]
            const point4 = [dx4, y1]
            const point5 = [x1, y1]
            if (dx2 && dx3) {
                const point2 = [dx2, (y0 + y1) / 2],
                    point3 = [dx3, (y0 + y1) / 2]
                return `M${point0} L${point1} L${point2} L${point3} L${point4} L${point5}`
            }
            return `M${point0} L${point1} L${point4} L${point5}`
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
                    // Because nodes are densely packed,  this adds an extra radius of 100 pixels to the nodes
                    return Math.sqrt(width * width + height * height) / 2 + 100
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
    background: white;
    .entity-fields {
        width: 100%;
        border-spacing: 0px;
    }
}
</style>
