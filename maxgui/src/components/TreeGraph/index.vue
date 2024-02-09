<template>
    <div class="tree-graph-container fill-height">
        <v-icon class="svg-grid-bg" color="#e3e6ea">$vuetify.icons.mxs_gridBg</v-icon>
        <svg ref="svg" class="tree-graph" :width="dim.width" height="100%">
            <g
                id="node-group"
                :transform="
                    `translate(${nodeGroupTransform.x},
                ${nodeGroupTransform.y}) scale(${nodeGroupTransform.k})`
                "
            />
        </svg>
        <div
            v-sortable="draggable"
            class="node-div-wrapper"
            :style="{
                transform: `translate(${nodeGroupTransform.x}px,
                ${nodeGroupTransform.y}px) scale(${nodeGroupTransform.k})`,
            }"
        >
            <div
                v-for="node in nodeDivData"
                :key="node.id"
                class="rect-node"
                :node_id="node.id"
                :class="[
                    draggable ? 'draggable-rect-node' : '',
                    noDragNodes.includes(node.id) ? 'no-drag' : '',
                ]"
                :style="{
                    top: handleCenterRectNodeVert(node),
                    left: `${node.y}px`,
                }"
            >
                <div
                    v-if="node.children || node._children"
                    class="node__circle node__circle--clickable"
                    :style="{
                        border: `1px solid ${node.data.linkColor}`,
                        background: !node.children ? node.data.linkColor : 'white',
                    }"
                    @click="onNodeClick(node)"
                />
                <slot name="rect-node-content" :data="{ node }" />
            </div>
        </div>
    </div>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { select as d3Select } from 'd3-selection'
import { hierarchy, tree } from 'd3-hierarchy'
import 'd3-transition'
import { zoom, zoomIdentity } from 'd3-zoom'
import Sortable from 'sortablejs'
/*
If draggable props is true, this component emits the following events
@on-node-drag-start: e: Event. Starts dragging a rect-node
@on-node-dragging: e: Event, callback: (v: bool):void. If the callback returns true, it accepts the new position
@on-node-drag-end: e: Event. Dragging ended
*/
export default {
    name: 'tree-graph',
    directives: {
        sortable: {
            bind(el, binding, vnode) {
                if (binding.value) {
                    const options = {
                        swap: true,
                        group: vnode.context.$props.draggableGroup,
                        draggable: '.draggable-rect-node',
                        ghostClass: 'rect-node-ghost',
                        animation: 0,
                        forceFallback: true,
                        fallbackClass: vnode.context.$props.cloneClass,
                        filter: '.no-drag',
                        preventOnFilter: false,
                        onStart: e => vnode.context.$emit('on-node-drag-start', e),
                        onMove: e => {
                            vnode.context.$emit('on-node-dragging', e)
                            return false // cancel drop
                        },
                        onEnd: e => vnode.context.$emit('on-node-drag-end', e),
                    }
                    Sortable.create(el, options)
                }
            },
        },
    },
    props: {
        data: { type: Object, required: true },
        dim: { type: Object, required: true },
        draggable: { type: Boolean, default: false },
        draggableGroup: { type: Object, default: () => ({ name: 'tree-graph' }) },
        cloneClass: { type: String, default: 'drag-node-clone' },
        noDragNodes: { type: Array, default: () => [] }, // list of node ids that are not draggable
        nodeSize: { type: Object, default: () => ({ width: 320, height: 100 }) },
        layoutConf: { type: Object, default: () => {} },
        expandedNodes: { type: Array, default: () => [] },
        nodeDivHeightMap: { type: Object, default: () => {} },
    },
    data() {
        return {
            duration: 300,
            nodeDivData: [],
            root: {},
            nodeGroupTransform: { x: 24, y: this.dim.height / 2, k: 1 },
            nodeSizeChangesCount: 0,
        }
    },
    computed: {
        layout() {
            return this.$helpers.lodash.merge({ margin: { left: 24 } }, this.layoutConf)
        },
        treeLayout() {
            return tree().size([this.dim.height, this.dim.width])
        },
    },
    watch: {
        data: {
            deep: true,
            handler(v) {
                this.computeHrchyLayout(v)
                this.update(this.root)
            },
        },
        nodeSize(v) {
            if (this.nodeSizeChangesCount === 0) {
                this.nodeSizeChangesCount += 1
                // center root node should be run once
                this.$set(this.nodeGroupTransform, 'y', -v.height / 2)
            }
            this.update(this.root)
        },
    },
    mounted() {
        if (this.data) {
            this.computeHrchyLayout(this.data)
            this.initSvg()
            this.update(this.root)
        }
    },
    methods: {
        /**
         * compute a hierarchical layout
         * @param {Object} data - tree data
         */
        computeHrchyLayout(data) {
            this.root = hierarchy(data)
            // vertically center root node
            this.$set(this.root, 'x0', this.dim.height / 2)
            this.$set(this.root, 'y0', 0)
        },
        initSvg() {
            const { left } = this.layout.margin
            this.nodeGroupTransform = zoomIdentity
                .translate(left, -this.nodeSize.height / 2)
                .scale(1)
            // Draw svg tree-graph
            this.svg = d3Select(this.$refs.svg)
                .call(zoom().transform, this.nodeGroupTransform)
                .call(
                    zoom().on('zoom', e => {
                        this.nodeGroupTransform = e.transform
                    })
                )
                .on('dblclick.zoom', null)
            this.svgGroup = this.svg.select('g#node-group')
        },
        /**
         * @param {Object} param.node - node to be vertically centered
         * @param {Object} param.recHeight - height of the svg rect
         * @param {Object} param.divHeight - height of the rectangular div
         * @returns {String} px string
         */
        centerNode({ node, rectHeight, divHeight }) {
            return `${node.x + (rectHeight - divHeight) / 2}px`
        },
        /**
         * @param {Object} node - node to be vertically centered
         */
        handleCenterRectNodeVert(node) {
            let res = `${node.x}px`
            if (!this.expandedNodes.length) return res
            else {
                const rectHeight = this.nodeSize.height
                const divHeight = this.nodeDivHeightMap[node.id]
                if (rectHeight > divHeight) res = this.centerNode({ node, rectHeight, divHeight })
            }
            return res
        },
        /**
         * Return cubic bezier point to create a bezier curve line from source node to dest node
         * @param {Object} param.dest - hierarchy d3 destination node
         * @param {Object} param.src - hierarchy d3 source node
         * @returns {Object} - returns { start: [x0, y0], p1: [x1, y1], p2: [x2, y2], end: [x, y] }
         * start point cord: x0,y0
         * control point 1 cord: x1,y1
         * control point 2 cord: x2,y2
         * end point cord: x,y
         */
        getCubicBezierPoints({ dest, src }) {
            // since the graph is draw horizontally, node.y is x0 and node.x is y0
            let x0 = src.y + this.nodeSize.width,
                y0 = src.x + this.nodeSize.height / 2,
                x = dest.y, // ending point
                y = dest.x + this.nodeSize.height / 2, // ending point
                // curves
                x1 = (x0 + x) / 2,
                x2 = x1,
                y1 = y0,
                y2 = y
            return { start: [x0, y0], p1: [x1, y1], p2: [x2, y2], end: [x, y] }
        },
        /**
         * De Casteljau's algorithm
         * B(t) = (1 - t)^3*P0 + 3(1 - t)^2*t*P1 + 3(1 - t)t^2*P2 + t^3*P3
         * @param {Array} param.start - starting point cord [x,y] (P0)
         * @param {Array} param.p1 - point 1 cord [x,y] (P1)
         * @param {Array} param.p2 - point 2 cord [x,y] (P2)
         * @param {Array} param.end - ending point cord [x,y] (P3)
         * @returns {Function} - returns interpolator function which returns a point coord {x,y} at t
         */
        interpolateCubicBezier({ start, p1, p2, end }) {
            /**
             * @param {Number} t - arbitrary parameter value 0 <= t <= 1
             * @returns {Object} - point coord {x,y}
             */
            return t => ({
                x:
                    Math.pow(1 - t, 3) * start[0] +
                    3 * Math.pow(1 - t, 2) * t * p1[0] +
                    3 * (1 - t) * Math.pow(t, 2) * p2[0] +
                    Math.pow(t, 3) * end[0],
                y:
                    Math.pow(1 - t, 3) * start[1] +
                    3 * Math.pow(1 - t, 2) * t * p1[1] +
                    3 * (1 - t) * Math.pow(t, 2) * p2[1] +
                    Math.pow(t, 3) * end[1],
            })
        },
        /**
         * B'(t) = 3(1- t)^2(P1 - P0) + 6(1 - t)t(P2 - P1) + 3t^2(P3 - P2)
         * https://en.wikipedia.org/wiki/B%C3%A9zier_curve
         * @param {Array} param.start - starting point cord [x,y] (P0)
         * @param {Array} param.p1 - point 1 cord [x,y] (P1)
         * @param {Array} param.p2 - point 2 cord [x,y] (P2)
         * @param {Array} param.end - ending point cord [x,y] (P3)
         * @returns {Function} - returns interpolator function which returns a point coord {x,y}
         */
        interpolateAngle({ start, p1, p2, end }) {
            /**
             * @param {Number} t - arbitrary parameter value 0 <= t <= 1
             * @returns {Number} - returns angle of the point
             */
            return function interpolator(t) {
                const tangentX =
                    3 * Math.pow(1 - t, 2) * (p1[0] - start[0]) +
                    6 * (1 - t) * t * (p2[0] - p1[0]) +
                    3 * Math.pow(t, 2) * (end[0] - p2[0])
                const tangentY =
                    3 * Math.pow(1 - t, 2) * (p1[1] - start[1]) +
                    6 * (1 - t) * t * (p2[1] - p1[1]) +
                    3 * Math.pow(t, 2) * (end[1] - p2[1])

                return Math.atan2(tangentY, tangentX) * (180 / Math.PI)
            }
        },
        /**
         *
         * @param {Object} param.dest - hierarchy d3 destination node
         * @param {Object} param.src - hierarchy d3 source node
         * @param {Number} param.numOfPoints - Divide a single Cubic Bézier curves into number of points
         * @param {Number} param.pointIdx - index of point to be returned
         * @returns {Object} - obj point at provided pointIdx {position, angle}
         */
        getRotatedPoint({ dest, src, numOfPoints, pointIdx }) {
            const cubicBezierPoints = this.getCubicBezierPoints({ dest, src })
            const cubicInterpolator = this.interpolateCubicBezier(cubicBezierPoints)
            const cubicAngleInterpolator = this.interpolateAngle(cubicBezierPoints)
            const t = pointIdx / (numOfPoints - 1)
            return {
                position: cubicInterpolator(t),
                angle: cubicAngleInterpolator(t),
            }
        },
        /**
         * Creates a Cubic Bézier curves path from source node to the destination nodes
         * @param {Object} param.dest - hierarchy d3 destination node
         * @param {Object} param.src - hierarchy d3 source node
         */
        diagonal({ dest, src }) {
            const { start, p1, p2, end } = this.getCubicBezierPoints({ dest, src })
            return `M ${start} C ${p1}, ${p2}, ${end}`
        },
        /**
         * Collapse the node and all it's children
         * @param {Object} node - hierarchy d3 node
         */
        collapseNode(node) {
            if (node.children) {
                node._children = node.children
                node._children.forEach(this.collapseNode)
                node.children = null
            }
        },
        /**
         * Toggle node on click.
         * @param {Object} node - hierarchy d3 node
         */
        onNodeClick(node) {
            if (node.children) {
                //collapse
                node._children = node.children
                node.children = null
            } else {
                // expand
                node.children = node._children
                node._children = null
            }
            this.update(node)
        },
        /**
         * @param {Object} srcNode - source node
         * @param {Object} linkGroup - linkGroup
         *  @param {String} type - enter, update or exit
         */
        drawLine({ srcNode, linkGroup, type }) {
            const className = 'link_line'
            const strokeWidth = 2.5
            switch (type) {
                case 'enter':
                    linkGroup
                        .append('path')
                        .attr('class', className)
                        .attr('fill', 'none')
                        .attr('stroke-width', strokeWidth)
                        .attr('stroke', d => d.data.linkColor)
                        .attr('d', () =>
                            this.diagonal({
                                // start at the right edge of the rect node
                                dest: { x: srcNode.x0, y: srcNode.y0 + this.nodeSize.width },
                                src: { x: srcNode.x0, y: srcNode.y0 },
                            })
                        )
                    break
                case 'update':
                    linkGroup
                        .select(`path.${className}`)
                        .attr('d', d => this.diagonal({ dest: d, src: d.parent }))
                    break
                case 'exit':
                    linkGroup.select(`path.${className}`).attr('d', () =>
                        this.diagonal({
                            // end at the right edge of the rect node
                            dest: { x: srcNode.x, y: srcNode.y + this.nodeSize.width },
                            src: srcNode,
                        })
                    )
                    break
            }
        },
        /**
         * @param {Object} srcNode - source node
         * @param {Object} linkGroup - linkGroup
         * @param {String} type - enter, update or exit
         */
        drawArrowHead({ srcNode, linkGroup, type }) {
            const className = 'link__arrow'
            switch (type) {
                case 'enter':
                    linkGroup
                        .append('path')
                        .attr('class', className)
                        .attr('stroke-width', 3)
                        .attr('d', 'M12,0 L-5,-8 L0,0 L-5,8 Z')
                        .attr('stroke-linecap', 'round')
                        .attr('stroke-linejoin', 'round')
                        .attr('transform', () => {
                            let o = {
                                x: srcNode.x0,
                                // start at the right edge of the rect node
                                y: srcNode.y0 + this.nodeSize.width,
                            }
                            const p = this.getRotatedPoint({
                                dest: o,
                                src: { x: srcNode.x0, y: srcNode.y0 },
                                numOfPoints: 10,
                                pointIdx: 0,
                            })
                            return `translate(${p.position.x}, ${p.position.y})`
                        })

                    break
                case 'update':
                    linkGroup
                        .select(`path.${className}`)
                        .attr('fill', d => d.data.linkColor)
                        .attr('transform', d => {
                            const p = this.getRotatedPoint({
                                dest: d,
                                src: d.parent,
                                numOfPoints: 10,
                                pointIdx: 7, // show arrow at point 7
                            })
                            return `translate(${p.position.x}, ${p.position.y}) rotate(${p.angle})`
                        })
                    break
                case 'exit':
                    linkGroup
                        .select(`path.${className}`)
                        .attr('fill', 'transparent')
                        .attr('transform', () => {
                            const p = this.getRotatedPoint({
                                dest: {
                                    x: srcNode.x, // end at the right edge of the rect node
                                    y: srcNode.y + this.nodeSize.width,
                                },
                                src: srcNode,
                                numOfPoints: 10,
                                pointIdx: 0,
                            })
                            return `translate(${p.position.x}, ${p.position.y})`
                        })
                    break
            }
        },
        drawArrowLink(param) {
            this.drawLine(param)
            this.drawArrowHead(param)
        },
        drawLinks({ srcNode, links }) {
            // Update the links...
            let linkGroup
            this.svgGroup
                .selectAll('.link-group')
                .data(links, d => d.id)
                .join(
                    enter => {
                        // insert after .node
                        linkGroup = enter.insert('g', 'g.node').attr('class', 'link-group')
                        this.drawArrowLink({ srcNode, linkGroup, type: 'enter' })
                        return linkGroup
                    },
                    // update is called when node changes it size
                    update => {
                        linkGroup = update
                            .merge(linkGroup)
                            .transition()
                            .duration(this.duration)
                        this.drawArrowLink({ srcNode, linkGroup, type: 'update' })
                        return linkGroup
                    },
                    exit => {
                        let linkGroup = exit
                            .transition()
                            .duration(this.duration)
                            .remove()
                        this.drawArrowLink({ srcNode, linkGroup, type: 'exit' })
                        return linkGroup
                    }
                )
        },
        /**
         * Update node
         * @param {Object} srcNode - hierarchy d3 node to be updated
         */
        update(srcNode) {
            // Recompute x,y coord for all nodes
            let treeData = this.treeLayout(this.root)
            // Compute the new tree layout.
            let nodes = treeData.descendants(),
                links = nodes.slice(1)
            this.breadthFirstTraversal(nodes, this.collision)
            // Normalize for fixed-depth.
            nodes.forEach(node => {
                node.y = node.depth * (this.nodeSize.width * 1.5)
                node.id = node.data.name
            })
            this.drawLinks({ srcNode, links })
            // Store the old positions for transition.
            nodes.forEach(d => {
                d.x0 = d.x
                d.y0 = d.y
            })
            this.nodeDivData = nodes
        },
        /**
         *   Breadth-first traversal of the tree
         *  cb function is processed on every node of a same level
         * @param {Array} nodes - flatten tree nodes
         * @param {Function} cb
         * @returns {Number} - max number of nodes on a the same level
         */
        breadthFirstTraversal(nodes, cb) {
            let max = 0
            if (nodes && nodes.length > 0) {
                let currentDepth = nodes[0].depth
                let fifo = []
                let currentLevel = []

                fifo.push(nodes[0])
                while (fifo.length > 0) {
                    let node = fifo.shift()
                    if (node.depth > currentDepth) {
                        cb(currentLevel)
                        currentDepth++
                        max = Math.max(max, currentLevel.length)
                        currentLevel = []
                    }
                    currentLevel.push(node)
                    if (node.children) {
                        for (let j = 0; j < node.children.length; j++) {
                            fifo.push(node.children[j])
                        }
                    }
                }
                cb(currentLevel)
                return Math.max(max, currentLevel.length)
            }
            return 0
        },

        /**
         * This handles add a padding of 10px to x property of a node
         * which represents y cord as the graph is render horizontally
         * @param {Array} siblings - siblings nodes
         */
        collision(siblings) {
            let minPadding = 10
            if (siblings) {
                for (let i = 0; i < siblings.length - 1; i++) {
                    if (siblings[i + 1].x - (siblings[i].x + this.nodeSize.height) < minPadding)
                        siblings[i + 1].x = siblings[i].x + this.nodeSize.height + minPadding
                }
            }
        },
    },
}
</script>

<style lang="scss" scoped>
.tree-graph-container {
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
    ::v-deep.tree-graph {
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
        .rect-node:not(.drag-node-clone) {
            position: absolute;
            background: transparent;
            .node__circle {
                position: absolute;
                z-index: 4;
                top: calc(50% + 7px);
                left: 0;
                transform: translate(-50%, -100%);
                width: 14px;
                height: 14px;
                border-radius: 50%;
                transition: all 0.1s linear;
                &--clickable {
                    cursor: pointer;
                    left: unset;
                    right: 0;
                    transform: translate(50%, -100%);
                    &:hover {
                        width: 16.8px;
                        height: 16.8px;
                    }
                }
            }
        }
    }
}
.draggable-rect-node:not(.no-drag) {
    cursor: move;
}

.rect-node-ghost {
    background: $tr-hovered-color !important;
    opacity: 0.6;
}
.drag-node-clone {
    opacity: 1 !important;
}
</style>
