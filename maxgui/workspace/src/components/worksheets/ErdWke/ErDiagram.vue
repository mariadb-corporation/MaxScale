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
                        <table class="entity-table">
                            <thead>
                                <tr :style="{ height: `${entityHeaderHeight}px` }">
                                    <th
                                        class="px-4 rounded-tr-lg rounded-tl-lg py-1 text-center font-weight-bold"
                                        :style="{
                                            backgroundColor: node.data.highlightColor,
                                            color: node.data.headerTxtColor,
                                        }"
                                    >
                                        {{ node.id }}
                                    </th>
                                </tr>
                            </thead>
                            <tbody>
                                <tr
                                    v-for="(col, i) in node.data.cols"
                                    :key="`key_${node.id}_${col.name}`"
                                    :style="{ height: `${trHeight}px` }"
                                >
                                    <td
                                        class="px-2"
                                        :class="{
                                            'rounded-bl-lg rounded-br-lg':
                                                i === node.data.cols.length - 1,
                                        }"
                                        :style="{
                                            borderLeft: getBorderStyle(node),
                                            borderRight: getBorderStyle(node),
                                            borderBottom:
                                                i === node.data.cols.length - 1
                                                    ? getBorderStyle(node)
                                                    : 'none',
                                        }"
                                    >
                                        {{ col.name }}
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
import GraphBoard from '@share/components/common/MxsCharts/GraphBoard.vue'
import GraphNodes from '@share/components/common/MxsCharts/GraphNodes.vue'
import {
    drawLinks,
    changeLinkGroupStyle,
    drawLink,
    getLinkCtr,
} from '@share/components/common/MxsCharts/utils'
import { LINK_SHAPES, createPath, genPath } from '@share/components/common/MxsCharts/linkShapes'

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
            pathPointsMap: {}, // keyed by link id
            //TODO: Add input to change this value
            linkShapeType: LINK_SHAPES.ORTHO,
        }
    },
    computed: {
        // Ensure that the marker remains visible while dragging a node by allocating a specific width.
        relMarkFixedWidth() {
            return 30
        },
        allLinks() {
            return this.simulation.force('link').links()
        },
        // Reserve 4 px to make sure point won't be at the top or bottom edge of the row
        rowHeightOffset() {
            return 4
        },
        // flat links into points and caching its link data and positions of the relational column
        connPoints() {
            return Object.values(this.allLinks).reduce((points, link) => {
                const {
                    source,
                    target,
                    pathData: { srcYPos, targetYPos },
                } = link
                const { x0, x1 } = this.pathPointsMap[link.id]
                // range attribute helps to detect overlapped points
                points = [
                    ...points,
                    {
                        id: source.id,
                        range: `${x0},${srcYPos.center}`,
                        yPositions: srcYPos,
                        linkedPointYPositions: targetYPos,
                        isSrc: true,
                        linkData: link,
                    },
                    {
                        id: target.id,
                        range: `${x1},${targetYPos.center}`,
                        yPositions: targetYPos,
                        linkedPointYPositions: srcYPos,
                        isSrc: false,
                        linkData: link,
                    },
                ]
                return points
            }, [])
        },
        /**
         * Generates a map of points that overlap in the `connPoints` array.
         * @returns {Object} - An object where the keys are link IDs and the values are arrays
         * of points that overlap. The array of points always has length >= 2
         */
        overlappedPoints() {
            // Group points have the same range
            let groupedPoints = this.$helpers.lodash.groupBy(this.connPoints, point => point.range)
            // get overlapped points and sort them
            return Object.keys(groupedPoints).reduce((acc, group) => {
                const points = groupedPoints[group]
                if (points.length > 1) {
                    points.sort(
                        (a, b) => a.linkedPointYPositions.center - b.linkedPointYPositions.center
                    )
                    acc[group] = points
                }
                return acc
            }, {})
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
        linkPathGenerator(linkData) {
            const { path, data } = genPath({
                shapeType: this.linkShapeType,
                linkData,
                entitySizeData: {
                    headerHeight: this.entityHeaderHeight,
                    rowHeight: this.trHeight,
                    rowOffset: this.rowHeightOffset,
                    markerWidth: this.relMarkFixedWidth,
                },
                nodeSizeMap: this.dynNodeSizeMap,
            })
            this.$set(this.pathPointsMap, linkData.id, data)
            return path
        },
        /**
         * @param {Object} node -  node
         * @returns {Object} - { width: Number, height: Number}
         */
        getNodeSize(node) {
            return this.$refs.graphNodes.getNodeSize(this.$typy(node, 'id').safeString)
        },
        linkStrokeGenerator(d) {
            return this.$typy(d, 'linkStyles.linkColor').safeString || 'black'
        },
        handleDrawLinks() {
            drawLinks({
                containerEle: this.svgGroup,
                data: this.allLinks,
                linkPathGenerator: this.linkPathGenerator,
                linkStrokeGenerator: this.linkStrokeGenerator,
            })
            this.repositionOverlappedPoints()
        },
        /**
         * Repositions overlapped points for each entity,
         * so that each point is visible and aligned in the row.
         */
        repositionOverlappedPoints() {
            Object.values(this.overlappedPoints).forEach(points => {
                // divide the row into points.length equal parts
                const k = (this.trHeight - this.rowHeightOffset) / (points.length + 1)
                // reposition points
                points.forEach((point, i) => {
                    const {
                        yPositions,
                        linkData: { id },
                    } = point
                    const newY = yPositions.top + k * i + k
                    let pathPoints = this.pathPointsMap[id]
                    // update coord
                    this.$set(pathPoints, point.isSrc ? 'y0' : 'y1', newY)
                    drawLink({
                        containerEle: getLinkCtr(id),
                        type: 'update',
                        isInvisible: false,
                        linkPathGenerator: createPath({
                            shapeType: this.linkShapeType,
                            ...pathPoints,
                        }),
                        linkStrokeGenerator: this.linkStrokeGenerator,
                    })
                })
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
            const links = this.allLinks
            this.highlightedLinks = links.filter(
                d => d.source.id === node.id || d.target.id === node.id
            )
            this.handleDrawLinks()
        },
        onNodeDragEnd() {
            this.isDraggingNode = false
        },
        getBorderStyle(node) {
            const { highlightColor } = node.data
            const style = `1px solid ${highlightColor}`
            return style
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
        }
    }
}
</style>
