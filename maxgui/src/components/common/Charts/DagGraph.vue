<template>
    <div class="dag-graph-container fill-height" :style="revertGraphStyle">
        <v-icon class="svg-grid-bg" color="#e3e6ea">$vuetify.icons.gridBg</v-icon>
        <svg ref="svg" class="dag-graph" :width="dim.width" height="100%">
            <g id="dag-node-group" :style="{ transform: nodeGroupTransformStyle }" />
        </svg>
        <div class="node-div-wrapper" :style="{ transform: nodeGroupTransformStyle }">
            <div
                v-for="node in nodeDivData"
                ref="rectNode"
                :key="node.data.id"
                class="rect-node"
                :class="{ move: draggable }"
                :node_id="node.data.id"
                :style="{
                    top: `${node.y}px`,
                    left: `${node.x}px`,
                    ...revertGraphStyle,
                    zIndex: draggingNodeId === node.data.id ? 4 : 3,
                }"
                v-on="
                    draggable
                        ? {
                              mousedown: e => onNodeDragStart({ e, node }),
                              mousemove: e => onNodeDragging({ e, node }),
                          }
                        : null
                "
            >
                <slot name="rect-node-content" :data="{ node, recompute }" />
            </div>
        </div>
    </div>
</template>

<script>
import { select as d3Select } from 'd3-selection'
import * as d3d from 'd3-dag'
import 'd3-transition'
import { zoom, zoomIdentity } from 'd3-zoom'

export default {
    name: 'dag-graph',
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
            duration: 0,
            dagDim: { width: 0, height: 0 }, // dag-node-group dim
            nodeGroupTransform: { x: 24, y: this.dim.height / 2, k: 1 },
            nodeDivData: [],
            defNodeHeight: 100,
            dynNodeHeightMap: {},
            heightChangesCount: 0,
            arrowHeadHeight: 12,
            // states for dragging conf-node
            isDragging: false,
            draggingNodeId: null,
            startPos: null,
        }
    },
    computed: {
        nodeGroupTransformStyle() {
            const { x, y, k } = this.nodeGroupTransform
            return `translate(${x}px, ${y}px) scale(${k})`
        },
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
    },
    watch: {
        data: {
            deep: true,
            handler() {
                this.recompute()
            },
        },
        dynNodeHeightMap: {
            deep: true,
            handler() {
                this.computeLayout(this.data)
                /**
                 * Because dynNodeHeightMap is computed after the first render of `rect-node-content`,
                 * the graph can only be centered accurately in the second render
                 */
                if (this.heightChangesCount === 0 || this.heightChangesCount === 1) {
                    this.heightChangesCount += 1
                    this.centerGraph()
                }
                this.update()
            },
        },
    },
    mounted() {
        if (this.data.length) {
            this.computeLayout(this.data)
            this.initSvg()
            this.update()
        }
    },
    beforeDestroy() {
        if (this.draggable) this.rmMouseUpEvt()
    },
    methods: {
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
        initSvg() {
            this.centerGraph()
            // Draw svg dag-graph
            this.svg = d3Select(this.$refs.svg)
                .call(zoom().on('zoom', e => (this.nodeGroupTransform = e.transform)))
                .on('dblclick.zoom', null)
            this.svgGroup = this.svg.select('g#dag-node-group')
        },
        update() {
            this.renderNodeDivs(this.dag.descendants())
            this.drawLinks(this.dag.links())
        },
        recompute() {
            this.computeLayout(this.data)
            this.update()
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
        // Vertically and horizontally Center graph
        centerGraph() {
            this.nodeGroupTransform = zoomIdentity
                .translate(
                    (this.dim.width - this.dagDim.width) / 2,
                    (this.dim.height - this.dagDim.height) / 2
                )
                .scale(1)
            // set initial transform
            this.svg = d3Select(this.$refs.svg).call(zoom().transform, this.nodeGroupTransform)
        },
        computeDynNodeHeight() {
            const rectNode = this.$typy(this.$refs, 'rectNode').safeArray
            let heightMap = {}
            rectNode.forEach(node => (heightMap[node.getAttribute('node_id')] = node.clientHeight))
            if (!this.$help.lodash.isEqual(this.dynNodeHeightMap, heightMap))
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
            let points = this.$help.lodash.cloneDeep(data.points)
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
        renderNodeDivs(nodes) {
            this.nodeDivData = nodes
            // compute node height after nodes are rendered
            if (this.dynNodeHeight) this.$help.doubleRAF(() => this.computeDynNodeHeight())
        },
        drawLinks(data) {
            let linkGroup = this.svgGroup.selectAll('.link-group').data(data)
            let linkGroupEnter = linkGroup
                .enter()
                .insert('g', 'g.node-rect-group') // insert after .node-rect-group
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

            linkGroupEnter
                .append('path')
                .attr('class', 'link_line')
                .attr('fill', 'none')
                .attr('stroke-width', 2.5)
                .attr('stroke-dasharray', '5')
                .attr('stroke', d => this.colorizingLinkFn(d) || '#0e9bc0')
                .attr('d', d => this.handleCreateDiagonal(d))
            linkGroupEnter
                .append('path')
                .attr('class', 'link__arrow')
                .attr('stroke-width', 3)
                .attr('d', 'M12,0 L-5,-8 L0,0 L-5,8 Z')
                .attr('stroke-linecap', 'round')
                .attr('stroke-linejoin', 'round')
                .attr('fill', d => this.colorizingLinkFn(d) || '#0e9bc0')
                .attr('transform', d => this.transformArrow(d))

            // UPDATE
            let linkGroupUpdate = linkGroupEnter.merge(linkGroup)
            // update link_line
            linkGroupUpdate
                .select('path.link_line')
                .transition()
                .duration(this.duration)
                .attr('stroke', d => this.colorizingLinkFn(d) || '#0e9bc0')
                .attr('d', d => this.handleCreateDiagonal(d))
            // update link__arrow
            linkGroupUpdate
                .select('path.link__arrow')
                .transition()
                .duration(this.duration)
                .attr('fill', d => this.colorizingLinkFn(d) || '#0e9bc0')
                .attr('transform', d => this.transformArrow(d))
            // Remove any exiting links
            linkGroup
                .exit()
                .transition()
                .duration(this.duration)
                .remove()
        },
        //-------------------------draggable methods---------------------------
        addMouseUpEvt() {
            document.addEventListener('mouseup', this.onNodeDragEnd)
        },
        rmMouseUpEvt() {
            document.removeEventListener('mouseup', this.onNodeDragEnd)
        },
        onNodeDragStart({ e, node }) {
            this.draggingNodeId = node.data.id
            this.startPos = { x: e.clientX, y: e.clientY }
            this.addMouseUpEvt()
        },
        onNodeDragging({ e, node }) {
            e.preventDefault()
            if (this.startPos && this.draggingNodeId === node.data.id) {
                this.isDragging = true
                const offsetPos = { x: e.clientX - this.startPos.x, y: e.clientY - this.startPos.y }
                this.startPos = { x: e.clientX, y: e.clientY }
                let offsetPosX = offsetPos.x / this.nodeGroupTransform.k,
                    offsetPosY = offsetPos.y / this.nodeGroupTransform.k
                // change pos of the dragging node
                if (this.revert) {
                    offsetPosX = -offsetPosX // graph is reverted, so minus offset
                    offsetPosY = -offsetPosY // graph is reverted, so minus offset
                }

                node.x = node.x + offsetPosX
                node.y = node.y + offsetPosY

                const dagNodes = this.dag.descendants()
                const dagNode = dagNodes.find(d => d.data.id === this.draggingNodeId)
                // change pos of child links
                for (const link of dagNode.ichildLinks()) {
                    let point = link.points[0]
                    point.x = point.x + offsetPosX
                    point.y = point.y + offsetPosY
                }
                // change pos of links to parent nodes
                dagNode.data.parentIds.forEach(parentId => {
                    const parentNode = dagNodes.find(d => d.data.id === parentId)
                    const linkToParent = parentNode
                        .childLinks()
                        .find(link => link.target.data.id === this.draggingNodeId)
                    let point = linkToParent.points[linkToParent.points.length - 1]
                    point.x = point.x + offsetPosX
                    point.y = point.y + offsetPosY
                })
                this.drawLinks(this.dag.links())
            }
        },
        onNodeDragEnd() {
            this.isDragging = false
            this.startPos = null
            this.rmMouseUpEvt()
        },
    },
}
</script>

<style lang="scss" scoped>
.dag-graph-container {
    width: 100%;
    position: relative;
    overflow: hidden;
    .svg-grid-bg {
        width: 100%;
        height: 100%;
        z-index: 1;
        pointer-events: none;
        background: transparent;
        position: absolute;
        left: 0;
    }
    ::v-deep.dag-graph {
        position: relative;
        left: 0;
        z-index: 2;
    }
    .node-div-wrapper {
        top: 0;
        height: 0;
        width: 0;
        position: absolute;
        z-index: 3;
        .rect-node {
            position: absolute;
            background: transparent;
        }
    }
}
</style>
