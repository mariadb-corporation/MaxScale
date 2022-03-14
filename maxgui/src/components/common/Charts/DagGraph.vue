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
                :node_id="node.data.id"
                :style="{
                    top: `${node.y}px`,
                    left: `${node.x}px`,
                    ...revertGraphStyle,
                }"
            >
                <slot name="rect-node-content" :data="{ node }" />
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
    },
    data() {
        return {
            duration: 300,
            dagDim: { width: 0, height: 0 }, // dag-node-group dim
            nodeGroupTransform: { x: 24, y: this.dim.height / 2, k: 1 },
            nodeDivData: [],
            defNodeHeight: 100,
            dynNodeHeightMap: {},
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
            handler(v) {
                this.computeLayout(v)
                this.update()
            },
        },
        dynNodeHeightMap: {
            deep: true,
            handler() {
                this.computeLayout(this.data)
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
    methods: {
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
        },
        initSvg() {
            this.nodeGroupTransform = zoomIdentity
                .translate(
                    (this.dim.width - this.dagDim.width) / 2,
                    (this.dim.height - this.dagDim.height) / 2
                )
                .scale(1)
            // Draw svg dag-graph
            this.svg = d3Select(this.$refs.svg)
                .call(zoom().transform, this.nodeGroupTransform)
                .call(
                    zoom().on('zoom', e => {
                        this.nodeGroupTransform = e.transform
                    })
                )
                .on('dblclick.zoom', null)
            this.svgGroup = this.svg.select('g#dag-node-group')
        },
        update() {
            let nodes = this.dag.descendants(),
                links = this.dag.links()
            nodes.forEach(d => {
                const { width, height } = this.getDynNodeSize(d)
                d.x = d.x - width / 2
                d.y = d.y - height / 2
            })
            links.forEach(d => {
                d.points.forEach((p, i) => {
                    if (i === 0) p.y = p.y + this.getDynNodeSize(d.source).height / 2
                    else p.y = p.y - this.getDynNodeSize(d.target).height / 2
                })
            })
            this.drawLinks(links)
            this.nodeDivData = nodes
            if (this.dynNodeHeight) this.$nextTick(() => this.computeDynNodeHeight())
        },
        computeDynNodeHeight() {
            const rectNode = this.$typy(this.$refs, 'rectNode').safeArray
            let heightMap = {}
            rectNode.forEach(node => (heightMap[node.getAttribute('node_id')] = node.clientHeight))
            if (!this.$help.lodash.isEqual(this.dynNodeHeightMap, heightMap))
                this.dynNodeHeightMap = heightMap
        },
        /**
         * Creates a polyline between nodes where it draws from the source point
         * to the vertical middle point (middle point between source.y and target.y) as
         * a straight line. Then it draws from that midpoint to the source point which is
         * perpendicular to the source node
         * @param {Array} points - Points from source to target node. There could be more than 2 points
         */
        obtuseShape(points) {
            const src = points[0]
            const target = points[points.length - 1] // d3-dag could provide more than 2 points.
            const midPoint = [target.x, src.y + (target.y - src.y) / 2]
            const h = target.x // horizontal line from source to target
            return `M ${src.x} ${src.y} ${midPoint} H ${h} L ${target.x} ${target.y}`
        },
        handleCreateDiagonal(data) {
            let points = data.points
            let shouldRevert = this.handleRevertDiagonal(data)
            if (shouldRevert) points = points.reverse()
            return this.obtuseShape(points)
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
        transformArrow(data) {
            const { points } = data
            let arrowPoint = points[points.length - 1]
            let shouldRevert = this.handleRevertDiagonal(data)
            const offset = shouldRevert ? 11.5 : -11.5
            const angle = shouldRevert ? 270 : 90
            return `translate(${arrowPoint.x}, ${arrowPoint.y + offset}) rotate(${angle})`
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
