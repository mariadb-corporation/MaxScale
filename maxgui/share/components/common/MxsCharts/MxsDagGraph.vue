<template>
    <graph-board
        class="mxs-dag-graph-container"
        :style="revertGraphStyle"
        :dim="dim"
        :graphDim="dagDim"
        @get-graph-ctr="svgGroup = $event"
        @get-zoom="zoom = $event"
    >
        <template v-slot:append="{ transform }">
            <graph-nodes
                ref="graphNodes"
                :nodes="graphNodes"
                :style="{ transform }"
                :nodeStyle="revertGraphStyle"
                :nodeClass="`${draggingStates.isDragging ? 'no-userSelect' : ''}`"
                draggable
                :draggingNodeId="draggingStates.draggingNodeId"
                @drag-start="onNodeDragStart"
                @drag="onNodeDrag"
                @drag-end="onNodeDragEnd"
            >
                <template v-slot:default="{ node }">
                    <slot
                        name="graph-node-content"
                        :data="{ node, draw, isDragging: draggingStates.isDragging }"
                    />
                </template>
            </graph-nodes>
        </template>
    </graph-board>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import { select as d3Select } from 'd3-selection'
import * as d3d from 'd3-dag'
import 'd3-transition'
import GraphBoard from '@share/components/common/MxsCharts/GraphBoard.vue'
import GraphNodes from '@share/components/common/MxsCharts/GraphNodes.vue'

export default {
    name: 'mxs-dag-graph',
    components: {
        'graph-board': GraphBoard,
        'graph-nodes': GraphNodes,
    },
    props: {
        data: { type: Array, required: true },
        dim: { type: Object, required: true },
        nodeWidth: { type: Number, default: 200 },
        dynNodeHeight: { type: Boolean, default: false },
        revert: { type: Boolean, default: false },
        colorizingLinkFn: { type: Function, default: () => '' },
        handleRevertDiagonal: { type: Function, default: () => false },
        draggable: { type: Boolean, default: false },
    },
    data() {
        return {
            svgGroup: null,
            zoom: { x: 0, y: 0, k: 1 },
            dagDim: { width: 0, height: 0 },
            graphNodes: [],
            defNodeHeight: 100,
            dynNodeHeightMap: {},
            arrowHeadHeight: 12,
            // states for dragging conf-node
            defDraggingStates: {
                isDragging: false,
                draggingNodeId: null,
                startPos: null,
            },
            draggingStates: null,
        }
    },
    computed: {
        revertGraphStyle() {
            return { transform: this.revert ? 'rotate(180deg)' : 'rotate(0d)' }
        },
        maxNodeHeight() {
            const v = Math.max(...Object.values(this.dynNodeHeightMap))
            if (this.$typy(v).isNumber) return v
            return this.defNodeHeight
        },
        defNodeSize() {
            return { width: this.nodeWidth, height: this.maxNodeHeight }
        },
        nodeIds() {
            return this.data.map(n => n.id)
        },
    },
    watch: {
        nodeIds: {
            deep: true,
            handler(v, oV) {
                if (!this.$helpers.lodash.isEqual(v, oV)) this.draw()
            },
        },
        dynNodeHeightMap: {
            deep: true,
            handler() {
                this.draw()
            },
        },
    },
    created() {
        if (this.draggable) this.setDefDraggingStates()
    },
    mounted() {
        if (this.data.length) this.draw()
    },
    methods: {
        setDefDraggingStates() {
            this.draggingStates = this.$helpers.lodash.cloneDeep(this.defDraggingStates)
        },
        draw() {
            this.computeLayout(this.data)
            this.render()
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
                .coord(d3d.coordGreedy())
                .sugiNodeSize(d => {
                    let width = this.defNodeSize.width,
                        height = this.defNodeSize.height
                    if (d.data.node) {
                        const nodeSize = this.getDynNodeSize(d.data.node)
                        width = nodeSize.width
                        height = nodeSize.height
                    }
                    // plus padding for each node as nodes are densely packed
                    return [width + 20, height + 60]
                })

            const { width, height } = this.layout(this.dag)
            this.dagDim = { width, height }
            this.repositioning()
        },
        render() {
            this.renderGraphNodes(this.dag.descendants())
            this.drawLinks(this.dag.links())
        },
        /**
         * Either return dynamic node size of a node or defNodeSize
         * @param {Object} node - dag node
         * @returns {Object} - { width: Number, height: Number}
         */
        getDynNodeSize(node) {
            const nodeId = this.$typy(node, 'data.id').safeString
            const nodeHeight = this.$typy(this.dynNodeHeightMap, `[${nodeId}]`).safeNumber
            if (nodeHeight) return { width: this.defNodeSize.width, height: nodeHeight }
            return this.defNodeSize
        },
        computeDynNodeHeight() {
            const heightMap = this.$typy(this.$refs, 'graphNodes.getHeightMap').safeFunction()
            if (!this.$helpers.lodash.isEqual(this.dynNodeHeightMap, heightMap))
                this.dynNodeHeightMap = heightMap
        },
        // Repositioning nodes and links by mutating x,y value
        repositioning() {
            let nodes = this.dag.descendants(),
                links = this.dag.links()
            // repositioning nodes so that they are drawn center
            nodes.forEach(d => {
                const { width, height } = this.getDynNodeSize(d)
                d.x = d.x - width / 2
                d.y = d.y - height / 2
            })
            // repositioning links so that links are drawn at the middle point of the edge
            links.forEach(d => {
                let shouldRevert = this.handleRevertDiagonal(d)
                const src = d.points[0]
                const target = d.points[d.points.length - 1]

                const srcSize = this.getDynNodeSize(d.source)
                const targetSize = this.getDynNodeSize(d.target)
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
                // change pos of src point
                points.srcX = src.x + srcXOffset
                points.srcY = src.y + srcYOffset
                // change pos of target point
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
                    src: this.getDynNodeSize(shouldRevert ? data.target : data.source),
                    target: this.getDynNodeSize(shouldRevert ? data.source : data.target),
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
        handleCreateDiagonal(data) {
            return this.obtuseShape(data)
        },
        transformArrow(data) {
            let { targetX, targetY, angle } = this.getObtusePoints(data)
            return `translate(${targetX}, ${targetY}) rotate(${angle})`
        },
        renderGraphNodes(nodes) {
            this.graphNodes = nodes
            // compute node height after nodes are rendered
            if (this.dynNodeHeight) this.$helpers.doubleRAF(() => this.computeDynNodeHeight())
        },
        /**
         * @param {Object} d - link data or node data
         * @returns {String} - color
         */
        colorize(d) {
            return this.colorizingLinkFn(d) || '#0e9bc0'
        },
        /**
         * @param {Object} linkGroup - linkGroup
         * @param {String} type - enter or update
         * @param {Boolean} isInvisible - draw an invisible line with enough thickness so that
         * mouseover event on `.link-group` can be triggered easily
         */
        drawLine({ linkGroup, type, isInvisible }) {
            const className = isInvisible ? 'link_line__invisible' : 'link_line'
            const strokeWidth = isInvisible ? 12 : 2.5
            const strokeDasharray = isInvisible ? 0 : 5
            const stroke = isInvisible ? 'transparent' : this.colorize
            const diagonal = d => this.handleCreateDiagonal(d)
            switch (type) {
                case 'enter':
                    linkGroup
                        .append('path')
                        .attr('class', className)
                        .attr('fill', 'none')
                        .attr('stroke-width', strokeWidth)
                        .attr('stroke-dasharray', strokeDasharray)
                        .attr('stroke', stroke)
                        .attr('d', diagonal)
                    break
                case 'update':
                    linkGroup
                        .select(`path.${className}`)
                        .attr('stroke', stroke)
                        .attr('d', diagonal)
                    break
            }
        },
        /**
         * @param {Object} linkGroup - linkGroup
         * @param {String} type - enter or update
         */
        drawArrowHead({ linkGroup, type }) {
            const className = 'link__arrow'
            const strokeWidth = 3
            const transform = d => this.transformArrow(d)
            switch (type) {
                case 'enter':
                    linkGroup
                        .append('path')
                        .attr('class', className)
                        .attr('stroke-width', strokeWidth)
                        .attr('d', 'M12,0 L-5,-8 L0,0 L-5,8 Z')
                        .attr('stroke-linecap', 'round')
                        .attr('stroke-linejoin', 'round')
                        .attr('fill', this.colorize)
                        .attr('transform', transform)
                    break
                case 'update':
                    linkGroup
                        .select(`path.${className}`)
                        .attr('fill', this.colorize)
                        .attr('transform', transform)
                    break
            }
        },
        drawArrowLink(param) {
            this.drawLine(param)
            this.drawArrowHead(param)
        },
        drawLinks(data) {
            this.svgGroup
                .selectAll('.link-group')
                .data(data)
                .join(
                    enter => {
                        // insert after .node-rect-group
                        const linkGroup = enter
                            .insert('g', 'g.node-rect-group')
                            .attr('class', 'link-group pointer')
                            .style('opacity', 0.5)
                            .on('mouseover', function() {
                                d3Select(this)
                                    .style('opacity', 1)
                                    .style('z-index', 10)
                                    .select('path.link_line')
                                    .attr('stroke-dasharray', null)
                            })
                            .on('mouseout', function() {
                                d3Select(this)
                                    .style('opacity', 0.5)
                                    .style('z-index', 'unset')
                                    .select('path.link_line')
                                    .attr('stroke-dasharray', '5')
                            })
                        this.drawArrowLink({ linkGroup, type: 'enter' })
                        /**
                         * mouseover event on `.link-group` can only be triggered when mouseover "visiblePainted" path.
                         * i.e. the space between dots won't trigger the event. In addition, the line is thin making
                         * it hard to trigger the event.
                         * So draw an invisible line with enough thickness.
                         */
                        this.drawLine({ linkGroup, type: 'enter', isInvisible: true })
                        return linkGroup
                    },
                    // update is called when node changes it size or its position
                    update => {
                        const linkGroup = update
                        this.drawArrowLink({ linkGroup, type: 'update' })
                        this.drawLine({ linkGroup, type: 'update', isInvisible: true })
                        return linkGroup
                    },
                    exit => exit.remove()
                )
        },
        //-------------------------draggable methods---------------------------
        /**
         * This helps to turn the dashed link to solid while dragging and vice versa.
         * @param {Object} param.link - child link or parent link of a node
         * @param {Boolean} param.isDragging - is node dragging
         */
        changeLinkGroupStyle({ link, isDragging }) {
            this.svgGroup
                .selectAll('.link-group')
                .filter(d => {
                    return (
                        d.source.data.id === link.source.data.id &&
                        d.target.data.id === link.target.data.id
                    )
                })
                .style('opacity', isDragging ? 1 : 0.5)
                .style('z-index', isDragging ? 10 : 'unset')
                .select('path.link_line')
                .attr('stroke-dasharray', isDragging ? null : '5')
        },
        onNodeDragStart({ e, node }) {
            this.draggingStates = {
                ...this.draggingStates,
                draggingNodeId: node.data.id,
                startPos: { x: e.clientX, y: e.clientY },
            }
        },
        onNodeDrag({ e, node }) {
            e.preventDefault()
            const { startPos, draggingNodeId } = this.draggingStates
            if (startPos && draggingNodeId === node.data.id) {
                const offsetPos = { x: e.clientX - startPos.x, y: e.clientY - startPos.y }
                // calc offset position
                let offsetPosX = offsetPos.x / this.zoom.k,
                    offsetPosY = offsetPos.y / this.zoom.k
                // update startPos
                this.draggingStates = {
                    ...this.draggingStates,
                    isDragging: true,
                    startPos: { x: e.clientX, y: e.clientY },
                }

                // change pos of the dragging node
                if (this.revert) {
                    offsetPosX = -offsetPosX // graph is reverted, so minus offset
                    offsetPosY = -offsetPosY // graph is reverted, so minus offset
                }

                node.x = node.x + offsetPosX
                node.y = node.y + offsetPosY

                const dagNodes = this.dag.descendants()
                const dagNode = dagNodes.find(d => d.data.id === draggingNodeId)
                // change pos of child links
                for (const link of dagNode.ichildLinks()) {
                    let point = link.points[0]
                    point.x = point.x + offsetPosX
                    point.y = point.y + offsetPosY
                    this.changeLinkGroupStyle({ link, isDragging: true })
                }
                let parentLinks = []
                // change pos of links to parent nodes
                dagNode.data.parentIds.forEach(parentId => {
                    const parentNode = dagNodes.find(d => d.data.id === parentId)
                    const linkToParent = parentNode
                        .childLinks()
                        .find(link => link.target.data.id === draggingNodeId)
                    parentLinks.push(linkToParent)
                    let point = linkToParent.points[linkToParent.points.length - 1]
                    point.x = point.x + offsetPosX
                    point.y = point.y + offsetPosY
                    this.changeLinkGroupStyle({ link: linkToParent, isDragging: true })
                })
                // store links so that style applied to them can be reset to default after finish dragging
                this.$set(this.draggingStates, 'relatedLinks', [
                    ...dagNode.ichildLinks(),
                    ...parentLinks,
                ])

                this.drawLinks(this.dag.links())
            }
        },
        onNodeDragEnd() {
            //  reset style of links to default
            for (const link of this.$typy(this.draggingStates, 'relatedLinks').safeArray) {
                this.changeLinkGroupStyle({ link, isDragging: false })
            }
            this.setDefDraggingStates()
        },
    },
}
</script>
