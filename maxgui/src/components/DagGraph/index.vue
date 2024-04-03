<template>
    <mxs-svg-graph-board
        v-model="panAndZoom"
        class="dag-graph-container"
        :style="revertGraphStyle"
        :dim="dim"
        :graphDim="dagDim"
        @get-graph-ctr="linkContainer = $event"
    >
        <template v-slot:append="{ data: { style } }">
            <mxs-svg-graph-nodes
                ref="graphNodes"
                autoWidth
                :nodes="graphNodes"
                :coordMap.sync="graphNodeCoordMap"
                :style="style"
                :nodeStyle="revertGraphStyle"
                :defNodeSize="defNodeSize"
                draggable
                :revertDrag="revert"
                :boardZoom="panAndZoom.k"
                @node-size-map="onNodesRendered"
                @drag="onNodeDrag"
                @drag-end="onNodeDragEnd"
            >
                <template v-slot:default="{ data }">
                    <slot name="graph-node-content" :data="data" />
                </template>
            </mxs-svg-graph-nodes>
        </template>
    </mxs-svg-graph-board>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import * as d3d from 'd3-dag'
import 'd3-transition'
import GraphConfig from '@share/components/common/MxsSvgGraphs/GraphConfig'
import Link from '@share/components/common/MxsSvgGraphs/Link'
import { getLinkConfig, EVENT_TYPES } from '@share/components/common/MxsSvgGraphs/linkConfig'

export default {
    name: 'dag-graph',
    props: {
        data: { type: Array, required: true },
        dim: { type: Object, required: true },
        defNodeSize: { type: Object, default: () => ({ width: 200, height: 100 }) },
        revert: { type: Boolean, default: false },
        colorizingLinkFn: { type: Function, default: () => '' },
        handleRevertDiagonal: { type: Function, default: () => false },
        draggable: { type: Boolean, default: false },
    },
    data() {
        return {
            linkContainer: null,
            dagDim: { width: 0, height: 0 },
            graphNodes: [],
            graphNodeCoordMap: {},
            arrowHeadHeight: 12,
            isDraggingNode: false,
            chosenLinks: [],
            graphConfig: null,
            linkInstance: null,
            dagLinks: [],
            panAndZoom: { x: 0, y: 0, k: 1 },
        }
    },
    computed: {
        revertGraphStyle() {
            return { transform: this.revert ? 'rotate(180deg)' : 'rotate(0d)' }
        },
        nodeIds() {
            return this.data.map(n => n.id)
        },
    },
    watch: {
        /**
         * If the quantity of nodes changes or the size of a node changes, re-draw
         * the graph.
         */
        nodeIds: {
            deep: true,
            handler(v, oV) {
                if (!this.$helpers.lodash.isEqual(v, oV)) this.draw()
            },
        },
        // Assign new data for graphNodes
        data: {
            deep: true,
            immediate: true,
            handler(v, oV) {
                if (!this.$helpers.lodash.isEqual(v, oV)) this.graphNodes = v
            },
        },
    },
    created() {
        this.initGraphConfig()
    },
    methods: {
        onNodesRendered() {
            if (this.data.length) this.draw()
        },
        draw() {
            this.initLinkInstance()
            this.computeLayout(this.data)
            this.drawLinks()
            this.setGraphNodeCoordMap()
        },
        /**
         * compute dag layout
         * @param {Object} data - tree data
         */
        computeLayout(data) {
            this.dag = d3d.dagStratify()(data)
            this.layout = d3d
                .sugiyama() // base layout
                .layering(d3d.layeringSimplex())
                .decross(d3d.decrossTwoLayer()) // minimize number of crossings
                .coord(d3d.coordSimplex())
                .sugiNodeSize(d => {
                    const { width, height } = this.getDagNodeSize(d.data.node)
                    // plus padding for each node as nodes are densely packed
                    return [width + 20, height + 60]
                })

            const { width, height } = this.layout(this.dag)
            this.dagDim = { width, height }
            this.dagLinks = this.dag.links()
            this.repositioning()
        },
        setGraphNodeCoordMap() {
            this.graphNodeCoordMap = this.dag.descendants().reduce((map, n) => {
                const {
                    x,
                    y,
                    data: { id },
                } = n
                if (id) map[id] = { x, y }
                return map
            }, {})
        },
        initGraphConfig() {
            this.graphConfig = new GraphConfig(
                this.$helpers.lodash.merge(getLinkConfig(), {
                    link: {
                        color: this.colorize,
                        [EVENT_TYPES.HOVER]: { dashArr: '0' },
                        [EVENT_TYPES.DRAGGING]: { dashArr: '0' },
                    },
                })
            )
        },
        initLinkInstance() {
            this.linkInstance = new Link(this.graphConfig.config.link)
        },
        /**
         * @param {Object} node - dag node
         * @returns {Object} - { width: Number, height: Number}
         */
        getDagNodeSize(node) {
            return this.$refs.graphNodes.getNodeSize(this.$typy(node, 'data.id').safeString)
        },
        // Repositioning links by mutating x,y value
        repositioning() {
            // repositioning links so that links are drawn at the middle point of the edge
            this.dagLinks.forEach(d => {
                let shouldRevert = this.handleRevertDiagonal(d)
                const src = d.points[0]
                const target = d.points[d.points.length - 1]

                const srcSize = this.getDagNodeSize(d.source)
                const targetSize = this.getDagNodeSize(d.target)
                if (shouldRevert) {
                    // src becomes a target point and vice versa
                    src.y = src.y + srcSize.height / 2 + this.arrowHeadHeight
                    target.y = target.y - targetSize.height / 2
                } else {
                    src.y = src.y + srcSize.height / 2
                    target.y = target.y - targetSize.height / 2 - this.arrowHeadHeight
                }
            })
        },

        /**
         * Handle override value for midPoint of param.points
         * @param {Object} param.points - Obtuse points to be overridden
         * @param {Boolean} param.isOpposite - is src node opposite to target node
         */
        setMidPoint({ points, isOpposite }) {
            if (isOpposite)
                points.midPoint = [points.targetX, points.srcY + (points.targetY - points.srcY) / 2]
            else points.midPoint = [(points.srcX + points.targetX) / 2, points.targetY]
        },
        /**
         * Handle override value for targetY, srcY, angle, midPoint of param.points
         * @param {Boolean} param.shouldRevert - result of handleRevertDiagonal()
         * @param {Object} param.src - source point
         * @param {Object} param.target - target point
         * @param {Object} param.sizes - {src,target} source and target node size
         * @param {Object} param.points - Obtuse points to be overridden
         */
        setOppositePoints({ shouldRevert, src, target, sizes, points }) {
            points.angle = shouldRevert ? 90 : 270
            points.srcY = shouldRevert ? src.y + sizes.src.height : src.y - sizes.src.height
            points.targetY = shouldRevert
                ? target.y - sizes.target.height - this.arrowHeadHeight * 2
                : target.y + sizes.target.height + this.arrowHeadHeight * 2

            if (shouldRevert) this.setMidPoint({ points, isOpposite: true })
        },
        /**
         * Handle override value for srcX, srcY, targetX, targetY, midPoint, angle, h of param.points
         * @param {Boolean} param.shouldRevert - result of handleRevertDiagonal()
         * @param {Object} param.src - source point
         * @param {Object} param.target - target point
         * @param {Object} param.sizes - {src,target} source and target node size
         * @param {Object} param.points - Obtuse points to be overridden
         */
        setSideBySidePoints({ shouldRevert, src, target, sizes, points }) {
            let isRightward = src.x > target.x + sizes.target.width,
                isLeftward = src.x < target.x - sizes.target.width

            // calc offset
            const srcXOffset = isRightward ? -sizes.src.width / 2 : sizes.src.width / 2
            const srcYOffset = shouldRevert ? sizes.src.height / 2 : -sizes.src.height / 2
            const targetXOffset = isRightward
                ? sizes.target.width / 2 + this.arrowHeadHeight - 2
                : -sizes.target.width / 2 - this.arrowHeadHeight
            const targetYOffset = shouldRevert
                ? -sizes.target.height / 2 - this.arrowHeadHeight
                : sizes.target.height / 2 + this.arrowHeadHeight

            if (isRightward || isLeftward) {
                // change coord of src point
                points.srcX = src.x + srcXOffset
                points.srcY = src.y + srcYOffset
                // change coord of target point
                points.targetX = target.x + targetXOffset
                points.targetY = target.y + targetYOffset
                this.setMidPoint({ points, isOpposite: false })
                points.angle = isRightward ? 180 : 0
                points.h = points.targetX
            }
        },
        getObtusePoints(data) {
            let shouldRevert = this.handleRevertDiagonal(data)
            const dPoints = this.getPoints(data)
            const src = dPoints[0]
            const target = dPoints[dPoints.length - 1] // d3-dag could provide more than 2 points.
            const yGap = 24
            // set default points
            const points = {
                srcX: src.x,
                srcY: src.y,
                targetX: target.x,
                targetY: target.y,
                midPoint: [0, 0],
                h: target.x, // horizontal line from source to target,
                angle: shouldRevert ? 270 : 90,
            }
            this.setMidPoint({ points, isOpposite: true })

            let shouldChangeConnPoint = shouldRevert
                ? src.y - yGap <= target.y
                : src.y + yGap >= target.y

            if (shouldChangeConnPoint) {
                // get src and target node size
                const sizes = {
                    src: this.getDagNodeSize(shouldRevert ? data.target : data.source),
                    target: this.getDagNodeSize(shouldRevert ? data.source : data.target),
                }
                // Check if src node opposite to target node
                const isOpposite = shouldRevert
                    ? src.y + sizes.src.height < target.y - sizes.target.height - yGap
                    : src.y - sizes.src.height > target.y + sizes.target.height + yGap

                if (isOpposite) this.setOppositePoints({ shouldRevert, src, target, sizes, points })
                else this.setSideBySidePoints({ shouldRevert, src, target, sizes, points })
            }

            return points
        },
        /**
         * Creates a polyline between nodes where it draws from the source point
         * to the vertical middle point (middle point between source.y and target.y) as
         * a straight line. Then it draws from that midpoint to the source point which is
         * perpendicular to the source node
         * @param {Object} data - Link data
         */
        obtuseShape(data) {
            const { srcX, srcY, midPoint, h, targetX, targetY } = this.getObtusePoints(data)
            return `M ${srcX} ${srcY} ${midPoint} H ${h} L ${targetX} ${targetY}`
        },
        getPoints(data) {
            let points = this.$helpers.lodash.cloneDeep(data.points)
            let shouldRevert = this.handleRevertDiagonal(data)
            if (shouldRevert) points = points.reverse()
            return points
        },
        pathGenerator(data) {
            return this.obtuseShape(data)
        },
        transformArrow(data) {
            let { targetX, targetY, angle } = this.getObtusePoints(data)
            return `translate(${targetX}, ${targetY}) rotate(${angle})`
        },
        /**
         * @param {Object} d - link data or node data
         * @returns {String} - color
         */
        colorize(d) {
            return this.colorizingLinkFn(d) || '#0e9bc0'
        },
        /**
         * @param {Object} linkCtr - container element of the link
         * @param {String} joinType - enter or update
         */
        drawArrowHead({ linkCtr, joinType }) {
            const className = 'link__arrow'
            const transform = d => this.transformArrow(d)
            const opacity = d => this.linkInstance.getStyle(d, 'opacity')
            let arrowPaths
            switch (joinType) {
                case 'enter':
                    arrowPaths = linkCtr.append('path').attr('class', className)
                    break
                case 'update':
                    arrowPaths = linkCtr.select(`path.${className}`)
                    break
            }
            arrowPaths
                .attr('stroke-width', 3)
                .attr('d', 'M12,0 L-5,-8 L0,0 L-5,8 Z')
                .attr('stroke-linecap', 'round')
                .attr('stroke-linejoin', 'round')
                .attr('fill', this.colorize)
                .attr('transform', transform)
                .attr('opacity', opacity)
        },
        handleMouseOverOut({ link, linkCtr, pathGenerator, eventType }) {
            this.linkInstance.setEventStyles({ links: [link], eventType })
            this.linkInstance.drawPaths({ linkCtr, joinType: 'update', pathGenerator })
            this.drawArrowHead({ linkCtr: linkCtr, joinType: 'update' })
        },
        drawLinks() {
            this.linkInstance.drawLinks({
                containerEle: this.linkContainer,
                data: this.dagLinks,
                nodeIdPath: 'data.id',
                pathGenerator: this.pathGenerator,
                afterEnter: this.drawArrowHead,
                afterUpdate: this.drawArrowHead,
                events: {
                    mouseover: param =>
                        this.handleMouseOverOut.bind(this)({
                            ...param,
                            eventType: EVENT_TYPES.HOVER,
                        }),
                    mouseout: param =>
                        this.handleMouseOverOut.bind(this)({
                            ...param,
                            eventType: EVENT_TYPES.NONE,
                        }),
                },
            })
        },
        //-------------------------draggable methods---------------------------
        /**
         *
         * @param {String} param.nodeId - id of the node has links being redrawn
         * @param {Number} param.diffX - difference of old coordinate x and new coordinate x
         * @param {Number} param.diffY - difference of old coordinate y and new coordinate y
         */
        updateLinkPositions({ nodeId, diffX, diffY }) {
            const dagNodes = this.dag.descendants()
            const dagNode = dagNodes.find(d => d.data.id === nodeId)
            // change coord of child links
            for (const link of dagNode.ichildLinks()) {
                let point = link.points[0]
                point.x = point.x + diffX
                point.y = point.y + diffY
            }
            let parentLinks = []
            // change coord of links to parent nodes
            dagNode.data.parentIds.forEach(parentId => {
                const parentNode = dagNodes.find(d => d.data.id === parentId)
                const linkToParent = parentNode
                    .childLinks()
                    .find(link => link.target.data.id === nodeId)
                parentLinks.push(linkToParent)
                let point = linkToParent.points[linkToParent.points.length - 1]
                point.x = point.x + diffX
                point.y = point.y + diffY
            })
            // store links so that style applied to them can be reset to default after finish dragging
            this.chosenLinks = this.dagLinks.filter(
                d => d.source.data.id === nodeId || d.target.data.id === nodeId
            )
        },
        setEventLinkStyles(eventType) {
            this.linkInstance.setEventStyles({ links: this.chosenLinks, eventType })
            this.drawLinks()
        },
        onNodeDrag({ node, diffX, diffY }) {
            this.updateLinkPositions({ nodeId: node.id, diffX, diffY })
            if (!this.isDraggingNode) this.setEventLinkStyles(EVENT_TYPES.DRAGGING)
            this.isDraggingNode = true
            this.drawLinks()
        },
        onNodeDragEnd() {
            if (this.isDraggingNode) this.setEventLinkStyles(EVENT_TYPES.NONE)
            this.isDraggingNode = false
        },
    },
}
</script>
