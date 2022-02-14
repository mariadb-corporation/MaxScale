<template>
    <div class="tree-graph-container">
        <svg ref="svgGridBg" class="svg-grid-bg" />
        <svg ref="svg" class="tree-graph">
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
                    draggable ? 'draggable-rect-node drag-handle' : '',
                    noDragNodes.includes(node.id) ? 'no-drag' : '',
                ]"
                :style="{
                    top: `${node.top}px`,
                    left: `${node.left}px`,
                }"
            >
                <slot name="rect-node-content" :data="{ node }" />
            </div>
        </div>
    </div>
</template>

<script>
import { select as d3Select } from 'd3-selection'
import { hierarchy, tree } from 'd3-hierarchy'
import 'd3-transition'
import { zoom, zoomIdentity } from 'd3-zoom'
import Sortable from 'sortablejs'
/*
If draggable props is true, this component emits the following events
@on-node-dragStart: e: Event. Starts dragging a rect-node
@on-node-move: e: Event, callback: (v: bool):void. Move a node in the list
@on-node-dragend: e: Event. Node dragging ended
*/
export default {
    name: 'tree-graph',
    directives: {
        sortable: {
            bind(el, binding, vnode) {
                if (binding.value) {
                    const options = {
                        swap: true,
                        handle: '.drag-handle',
                        draggable: '.draggable-rect-node',
                        ghostClass: 'rect-node-ghost',
                        chosenClass: 'rect-node-chosen',
                        animation: 200,
                        forceFallback: true,
                        fallbackClass: 'rect-node-clone',
                        filter: '.no-drag',
                        onStart: e => {
                            vnode.context.$emit('on-node-dragStart', e)
                        },
                        onMove: e => {
                            let isDroppable = true
                            // emit on-node-move and provide callback to assign return value
                            vnode.context.$emit('on-node-move', e, v => (isDroppable = v))
                            if (e.related.classList.contains('no-drag')) return false
                            return isDroppable
                        },
                        onEnd: e => {
                            vnode.context.$emit('on-node-dragend', e)
                        },
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
        noDragNodes: { type: Array, default: () => [] }, // list of node ids that are not draggable
        nodeSize: { type: Object, default: () => ({ width: 320, height: 100 }) },
        layoutConf: { type: Object, default: () => {} },
    },
    data() {
        return {
            duration: 300,
            circleRadius: 7,
            nodeDivData: [],
            root: {},
            nodeGroupTransform: { x: 48, y: this.dim.height / 2, k: 1 },
            nodeSizeChangesCount: 0,
        }
    },
    computed: {
        layout() {
            return this.$help.lodash.deepMerge({ margin: { left: 48 } }, this.layoutConf)
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
            // Draw grid background
            let svgGridBg = d3Select(this.$refs.svgGridBg)
            let pattern = svgGridBg.append('defs').append('pattern')
            pattern
                .attr('id', 'grid')
                .attr('width', 60)
                .attr('height', 20)
                .attr('patternUnits', 'userSpaceOnUse')
                .append('line')
                .attr('x1', 4)
                .attr('x2', 60)
                .attr('y1', 20)
                .attr('y2', 20)
                .attr('stroke', '#e3e6ea')
                .attr('stroke-width', 2)
                .attr('stroke-dasharray', 4)
            pattern
                .insert('line')
                .attr('x1', 60)
                .attr('x2', 60)
                .attr('y1', 0)
                .attr('y2', 20)
                .attr('stroke', '#e3e6ea')
            const { left } = this.layout.margin
            svgGridBg
                .append('rect')
                .attr('width', '100%')
                .attr('height', '100%')
                .attr('fill', 'url(#grid)')
            this.nodeGroupTransform = zoomIdentity
                .translate(left, -this.nodeSize.height / 2)
                .scale(1)
            // Draw svg tree-graph
            this.svg = d3Select(this.$refs.svg)
                .attr('width', this.dim.width)
                .attr('height', this.dim.height)
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
         * Creates a curved path from source node to the destination nodes
         * @param {Object} src - hierarchy d3 source node
         * @param {Object} dest - hierarchy d3 destination node
         */ diagonal(src, dest) {
            return `M ${src.y} ${src.x}
            C ${(src.y + dest.y) / 2} ${src.x},
              ${(src.y + dest.y) / 2} ${dest.x},
              ${dest.y} ${dest.x}`
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
        drawNodes({ srcNode, nodes }) {
            // ****************** Nodes drawing transition ***************************
            // Declare the nodes…
            let node = this.svgGroup
                .selectAll('g.node')
                .data(nodes, node => (node.id = node.data.name))
            // Enter any new nodes at the parent's previous position.
            let nodeEnter = node
                .enter()
                .append('g')
                .attr('class', 'node')
                .attr('data-node-id', n => n.id)
                // use srcNode.y as x for translate to make horizontal graph
                .attr('transform', () => `translate(${srcNode.y0},${srcNode.x0})`)
                .on('click', (e, n) => {
                    e.stopPropagation()
                    this.onNodeClick(n)
                })

            nodeEnter
                .append('circle')
                .attr('class', 'node__circle')
                .attr('r', 0)

            // On node update
            let nodeUpdate = nodeEnter.merge(node)
            // Transition to the proper position for the node
            nodeUpdate
                .transition()
                .duration(this.duration)
                .attr('transform', d => `translate(${d.y},${d.x})`)

            // Update the node attributes and style
            nodeUpdate
                .select('circle.node__circle')
                .attr('class', d =>
                    d.children || d._children
                        ? 'node__circle node__circle--clickable'
                        : 'node__circle'
                )
                .attr('r', this.circleRadius)
                .style('fill', d => {
                    return d._children ? d.data.stroke : 'white'
                })
                .style('stroke', d => d.data.stroke)

            // Remove any exiting nodes on exit
            let nodeExit = node
                .exit()
                .transition()
                .duration(this.duration)
                .attr('transform', () => 'translate(' + srcNode.y + ',' + srcNode.x + ')')
                .remove()

            // On exit reduce the node circles size to 0
            nodeExit.select('circle.node__circle').attr('r', 0)
        },
        drawLinks({ srcNode, links }) {
            // Update the links...
            let linkGroup = this.svgGroup.selectAll('.link-group').data(links, d => d.id)

            // Enter any new links at the parent's previous position.
            let linkGroupEnter = linkGroup
                .enter()
                .insert('g', 'g.node')
                .attr('class', 'link-group')

            // create link_line
            linkGroupEnter
                .append('path')
                .attr('class', 'link_line')
                .attr('fill', 'none')
                .attr('stroke-width', 2.5)
                .attr('stroke', d => d.data.stroke)
                .attr('d', () => {
                    let o = { x: srcNode.x0, y: srcNode.y0 }
                    return this.diagonal(o, o)
                })
            // TODO: create link__arrow
            // UPDATE
            let linkGroupUpdate = linkGroupEnter.merge(linkGroup)
            // Transition back to the parent element position
            // update link_line
            linkGroupUpdate
                .select('path.link_line')
                .transition()
                .duration(this.duration)
                .attr('d', d => this.diagonal(d, d.parent))

            // Remove any exiting links
            let linkExit = linkGroup
                .exit()
                .transition()
                .duration(this.duration)
                .remove()

            // remove link_line
            linkExit
                .select('path.link_line')
                .attr('d', () => {
                    let o = { x: srcNode.x, y: srcNode.y }
                    return this.diagonal(o, o)
                })
                .remove()
        },
        renderRectNode(nodes) {
            let nodeDivData = []
            nodes.forEach(node => {
                let pos = { left: node.y + this.circleRadius + 10, top: node.x }
                nodeDivData.push({ ...node, ...pos })
            })
            this.nodeDivData = nodeDivData
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
            })
            this.drawNodes({ srcNode, nodes })
            this.drawLinks({ srcNode, links })
            // Store the old positions for transition.
            nodes.forEach(d => {
                d.x0 = d.x
                d.y0 = d.y
            })
            this.nodeDivData = nodes
            this.renderRectNode(this.nodeDivData)
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
        position: absolute;
        z-index: 1;
        left: 0;
        pointer-events: none;
        background: transparent;
    }
    ::v-deep.tree-graph {
        position: relative;
        left: 0;
        z-index: 2;
        .node__circle {
            &--clickable {
                cursor: pointer;
                &:hover {
                    transform: scale(1.2, 1.2);
                }
            }
        }
    }
    .node-div-wrapper {
        top: 0;
        height: 0;
        width: 0;
        position: absolute;
        z-index: 3;
        .rect-node:not(.rect-node-clone) {
            position: absolute;
            transform: translateY(-50%) !important;
            background-color: $background;
        }
    }
}
.draggable-rect-node:not(.no-drag) {
    cursor: move;
}

.rect-node-ghost {
    background: #f2fcff !important;
    opacity: 0.6;
}
.rect-node-clone {
    opacity: 1 !important;
}
</style>
