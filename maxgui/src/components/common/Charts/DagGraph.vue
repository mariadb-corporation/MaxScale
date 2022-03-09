<template>
    <div class="dag-graph-container" :style="revertGraphStyle">
        <v-icon class="svg-grid-bg" color="#e3e6ea">$vuetify.icons.gridBg</v-icon>
        <svg ref="svg" class="dag-graph" :width="dim.width" :height="dim.height">
            <g id="dag-node-group" :style="{ transform: nodeGroupTransformStyle }" />
        </svg>
        <div class="node-div-wrapper" :style="{ transform: nodeGroupTransformStyle }">
            <div
                v-for="node in nodeDivData"
                :key="node.id"
                class="rect-node"
                :node_id="node.id"
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
import { curveStepAfter, line } from 'd3-shape'
export default {
    name: 'dag-graph',
    props: {
        data: { type: Array, required: true },
        dim: { type: Object, required: true },
        nodeSize: { type: Object, required: true },
        dynNodeHeightMap: { type: Object, default: () => {} },
        revert: { type: Boolean, default: false },
    },
    data() {
        return {
            duration: 300,
            dagDim: { width: 0, height: 0 }, // dag-node-group dim
            nodeGroupTransform: { x: 24, y: this.dim.height / 2, k: 1 },
            nodeDivData: [],
        }
    },
    computed: {
        d3Line() {
            return line
        },
        nodeGroupTransformStyle() {
            const { x, y, k } = this.nodeGroupTransform
            return `translate(${x}px, ${y}px) scale(${k})`
        },
        revertGraphStyle() {
            return { transform: this.revert ? 'rotate(180deg)' : 'rotate(0d)' }
        },
    },
    watch: {
        data: {
            deep: true,
            handler(v) {
                if (v.length) {
                    this.computeLayout(v)
                    this.update()
                }
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
         * Either return dynamic node size of a node or nodeSize
         * @param {Object} node - dag node
         * @returns {Object} - { width: Number, height: Number}
         */
        getDynNodeSize(node) {
            const nodeId = this.$typy(node, 'data.id').safeString
            const nodeHeight = this.$typy(this.dynNodeHeightMap, `[${nodeId}]`).safeNumber
            if (nodeHeight) return { width: this.nodeSize.width, height: nodeHeight }
            return this.nodeSize
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
                .decross(d3d.decrossOpt()) // minimize number of crossings
                .coord(d3d.coordQuad())
                .nodeSize(() => [this.nodeSize.width * 1.2, this.nodeSize.height * 1.5])

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
            this.drawNodes(nodes)
            this.drawLinks(links)
            this.nodeDivData = nodes
        },
        drawNodes(data) {
            let nodes = this.svgGroup.selectAll('g.node-rect-group').data(data)
            // Enter any new nodes at the parent's previous position.
            let nodeEnter = nodes
                .enter()
                .append('g')
                .attr('class', 'node-rect-group')
                .attr('transform', d => `translate(${d.x}, ${d.y})`)
            nodeEnter
                .append('rect')
                .attr('width', d => this.getDynNodeSize(d).width)
                .attr('height', d => this.getDynNodeSize(d).height)
                .attr('class', 'node__rect')
                .style('visibility', 'hidden')

            // On node update, get dynamic nodeSize
            let nodeUpdate = nodeEnter.merge(nodes)
            nodeUpdate
                .transition()
                .duration(this.duration)
                .attr('transform', d => `translate(${d.x}, ${d.y})`)
            nodeUpdate
                .select('rect')
                .attr('width', d => this.getDynNodeSize(d).width)
                .attr('height', d => this.getDynNodeSize(d).height)
            // node exit
            nodes
                .exit()
                .transition()
                .duration(this.duration)
                .attr('transform', d => `translate(${d.x}, ${d.y})`)
                .remove()
        },

        diagonal(points) {
            //TODO: Use a different curve or customized curve
            const line = this.d3Line()
                .curve(curveStepAfter)
                .x(d => d.x)
                .y(d => d.y)
            return line(points)
        },
        drawLinks(data) {
            //TODO: Add arrow
            let linkGroup = this.svgGroup.selectAll('.link-group').data(data)
            let linkGroupEnter = linkGroup
                .enter()
                .insert('g', 'g.node-rect-group') // insert after .node-rect-group
                .attr('class', 'link-group')
            linkGroupEnter
                .append('path')
                .attr('class', 'link_line')
                .attr('fill', 'none')
                .attr('stroke-width', 2.5)
                .attr('stroke', () => {
                    //TODO: Use dynamic link color from node data
                    return '#0e9bc0'
                })
                .attr('d', ({ points }) => this.diagonal(points))
            // UPDATE
            let linkGroupUpdate = linkGroupEnter.merge(linkGroup)
            // update link_line
            linkGroupUpdate
                .select('path.link_line')
                .transition()
                .duration(this.duration)
                .attr('d', ({ points }) => this.diagonal(points))
            // Remove any exiting links
            let linkExit = linkGroup
                .exit()
                .transition()
                .duration(this.duration)
                .remove()
            // remove link_line
            linkExit
                .select('path.link_line')
                .attr('d', ({ points }) => this.diagonal(points))
                .remove()
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
