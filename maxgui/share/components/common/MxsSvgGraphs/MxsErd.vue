<template>
    <div class="fill-height er-diagram">
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
                                    :style="{ height: `${entitySizeConfig.rowHeight}px` }"
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
import GraphBoard from '@share/components/common/MxsSvgGraphs/GraphBoard.vue'
import GraphNodes from '@share/components/common/MxsSvgGraphs/GraphNodes.vue'
import Link from '@share/components/common/MxsSvgGraphs/Link'
import EntityLinkShape from '@share/components/common/MxsSvgGraphs/EntityLinkShape'
import { LINK_SHAPES } from '@share/components/common/MxsSvgGraphs/config'

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
            svgGroup: null,
            graphNodeCoordMap: {},
            graphNodes: [],
            graphLinks: [],
            simulation: null,
            defNodeSize: { width: 200, height: 100 },
            dynNodeSizeMap: {},
            isDraggingNode: false,
            highlightedLinks: [],
            entityHeaderHeight: 32,
            pathPointsMap: {}, // keyed by link id
            linkInstance: null,
            linkShapeInstance: null,
            //TODO: Add input to change this value
            linkShapeType: LINK_SHAPES.ORTHO,
        }
    },
    computed: {
        entitySizeConfig() {
            return { rowHeight: 32, rowOffset: 4 }
        },
        // flat links into points and caching its link data and positions of the relational column
        connPoints() {
            return Object.values(this.getLinks()).reduce((points, link) => {
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
            for (const link of this.highlightedLinks)
                this.linkInstance.changeLinkStyle({ link, isDragging: v })
        },
    },
    created() {
        this.isRendering = true
        this.assignData(this.data)
        // wait until graph-nodes is fully rendered so that handleCollision method can calculate the radius accurately
        this.$nextTick(() => this.runSimulation())
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
            this.linkInstance = new Link({
                strokeWidth: 1,
            })
            this.linkShapeInstance = new EntityLinkShape({
                type: this.linkShapeType,
                entitySizeConfig: this.entitySizeConfig,
                nodeSizeMap: this.dynNodeSizeMap,
            })
        },
        drawChart() {
            this.setGraphNodeCoordMap()
            this.initLinkInstance()
            this.handleDrawLinks()
        },
        setGraphNodeCoordMap() {
            this.graphNodeCoordMap = this.graphNodes.reduce((map, n) => {
                const { x, y, id } = n
                /**
                 * minus entityHeaderHeight so that EntityLinkShape can calculate the
                 * y position accurately
                 */
                if (id) map[id] = { x, y: y - this.entityHeaderHeight }
                return map
            }, {})
        },
        pathGenerator(linkData) {
            const { path, data } = this.linkShapeInstance.genPath(linkData)
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
        getLinks() {
            return this.simulation.force('link').links()
        },
        handleDrawLinks() {
            this.linkInstance.drawLinks({
                containerEle: this.svgGroup,
                data: this.getLinks(),
                pathGenerator: this.pathGenerator,
            })
            this.repositionOverlappedPoints()
        },
        /**
         * Repositions overlapped points for each entity,
         * so that each point is visible and aligned in the row.
         */
        repositionOverlappedPoints() {
            const { rowHeight, rowOffset } = this.entitySizeConfig
            Object.values(this.overlappedPoints).forEach(points => {
                // divide the row into points.length equal parts
                const k = (rowHeight - rowOffset) / (points.length + 1)
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
                    this.linkInstance.drawPath({
                        containerEle: this.linkInstance.getLinkCtr(id),
                        type: 'update',
                        pathGenerator: this.linkShapeInstance.createPath(pathPoints),
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
            const nodeData = this.graphNodes.find(n => n.id === node.id)
            nodeData.x = nodeData.x + diffX
            nodeData.y = nodeData.y + diffY
            this.highlightedLinks = this.getLinks().filter(
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
