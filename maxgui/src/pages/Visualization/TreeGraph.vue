<template>
    <div class="tree-graph-container">
        <svg ref="svgGridBg" class="svg-grid-bg" />
        <svg ref="svg" class="tree-graph" />
        <div
            ref="nodeRectWrapper"
            class="node-rect-wrapper"
            :style="{
                transform: `translate(${layout.margin.left}px, ${layout.margin.top}px)`,
            }"
        >
            <div
                v-for="(pos, key) in nodeRectPosMap"
                :key="key"
                class="node-rect"
                :style="{
                    top: `${pos.top}px`,
                    left: `${pos.left}px`,
                }"
            >
                <slot name="node-rect" :data="{ id: key }" />
            </div>
        </div>
    </div>
</template>

<script>
import { select as d3Select } from 'd3-selection'
import { hierarchy, tree } from 'd3-hierarchy'
import 'd3-transition'
import { zoom } from 'd3-zoom'
export default {
    name: 'tree-graph',
    props: {
        data: { type: Object, required: true },
        dim: { type: Object, required: true },
    },
    data() {
        return {
            duration: 300,
            layout: {
                link: {
                    length: 320,
                },
                margin: { top: 20, right: 20, bottom: 20, left: 48 },
            },
            circleRadius: 7,
            svg: null, // svg obj
            nodeRectPosMap: {},
            root: {},
        }
    },
    computed: {
        scrollBarThickness() {
            return this.$help.getScrollbarWidth()
        },
        // return the width/height of tree content after subtracting margin)
        treeDim() {
            const { top, right, bottom, left } = this.layout.margin
            return {
                width: this.dim.width - left - right,
                height: this.dim.height - top - bottom,
            }
        },
        treeLayout() {
            // create a tree layout and assigns the size of the tree
            return tree().size([this.treeDim.height, this.treeDim.width])
        },
        maxDepth() {
            const nodes = this.root.descendants() || []
            return nodes.length ? nodes[nodes.length - 1].depth : 1
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
    },
    mounted() {
        this.initSvg()
        if (this.data) {
            this.computeHrchyLayout(this.data)
            this.update(this.root)
        }
    },

    methods: {
        computeHrchyLayout(data) {
            // compute a hierarchical layout
            this.root = hierarchy(data)
            // vertically center root node
            this.$set(this.root, 'x0', this.treeDim.height / 2)
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

            svgGridBg
                .append('rect')
                .attr('width', '100%')
                .attr('height', '100%')
                .attr('fill', 'url(#grid)')

            // Draw svg tree-graph
            this.svg = d3Select(this.$refs.svg)
                .attr('width', this.dim.width)
                .attr('height', this.dim.height)
                .call(
                    zoom().on('zoom', e => {
                        this.svg.attr('transform', e.transform)
                        const { x, y, k } = e.transform
                        const transform = `translate(${x}px,${y}px) scale(${k})`
                        this.$refs.nodeRectWrapper.style.transform = transform
                    })
                )
                .append('g')
                .attr('id', 'node-group')
                .attr(
                    'transform',
                    'translate(' + this.layout.margin.left + ',' + this.layout.margin.top + ')'
                )
        },
        /**
         * Creates a curved or orthogonal (diagonal) path from source node to the destination nodes
         * For the tree has depth >= 3, using the orthogonal line between nodes helps to avoid
         * overlapping rectangular node div.
         * @param {Object} src - hierarchy d3 source node
         * @param {Object} dest - hierarchy d3 destination node
         */
        diagonal(src, dest) {
            if (this.maxDepth >= 3) return 'M' + src.y + ',' + src.x + 'V' + dest.x + 'H' + dest.y
            else
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
            let node = this.svg.selectAll('g.node').data(nodes, node => (node.id = node.data.name))
            // Enter any new nodes at the parent's previous position.
            let nodeEnter = node
                .enter()
                .append('g')
                .attr('class', 'node')
                .attr('data-node-id', n => n.id)
                // use srcNode.y as x for translate to make horizontal graph
                .attr('transform', () => 'translate(' + srcNode.y0 + ',' + srcNode.x0 + ')')
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
                .attr('transform', d => 'translate(' + d.y + ',' + d.x + ')')

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
            let link = this.svg.selectAll('path.link').data(links, d => d.id)

            // Enter any new links at the parent's previous position.
            let linkEnter = link
                .enter()
                .insert('path', 'g')
                .attr('class', 'link')
                .attr('d', () => {
                    let o = { x: srcNode.x0, y: srcNode.y0 }
                    return this.diagonal(o, o)
                })

            // UPDATE
            let linkUpdate = linkEnter.merge(link)

            // Transition back to the parent element position
            linkUpdate
                .transition()
                .duration(this.duration)
                .attr('d', d => this.diagonal(d, d.parent))

            // Remove any exiting links
            link.exit()
                .transition()
                .duration(this.duration)
                .attr('d', () => {
                    let o = { x: srcNode.x, y: srcNode.y }
                    return this.diagonal(o, o)
                })
                .remove()
        },
        renderNodeRect(nodes) {
            this.nodeRectPosMap = this.getNodeRectPos(nodes)
        },
        getNodeRectPos(nodes) {
            let nodeRectPosMap = {}
            nodes.forEach(node => {
                const nodeId = node.id
                let pos = { left: node.y + this.circleRadius + 10, top: node.x }
                nodeRectPosMap[nodeId] = pos
            })
            return nodeRectPosMap
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
                links = treeData.descendants().slice(1)
            // Normalize for fixed-depth.
            nodes.forEach(node => {
                node.y = node.depth * this.layout.link.length
            })
            this.drawNodes({ srcNode, nodes })
            this.drawLinks({ srcNode, links })
            // Store the old positions for transition.
            nodes.forEach(d => {
                d.x0 = d.x
                d.y0 = d.y
            })
            this.renderNodeRect(nodes)
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
        .link {
            fill: none;
            stroke: #e7eef1;
            stroke-width: 1.5px;
        }
        .node__circle {
            &--clickable {
                cursor: pointer;
                &:hover {
                    transform: scale(1.2, 1.2);
                }
            }
        }
    }
    .node-rect-wrapper {
        top: 0;
        height: 0;
        width: 0;
        position: absolute;
        z-index: 3;
        .node-rect {
            width: 276px;
            min-height: 50px;
            max-height: 100px;
            position: absolute;
            transform: translateY(-50%);
            box-shadow: 1px 1px 7px rgba(0, 0, 0, 0.1);
            border: 1px solid #e3e6ea;
            background-color: $background;
            &::after,
            &::before {
                right: 100%;
                top: 50%;
                border: solid transparent;
                content: ' ';
                height: 0;
                width: 0;
                position: absolute;
                pointer-events: none;
                box-sizing: border-box;
            }
            &::before {
                border-color: transparent;
                border-right-color: #e3e6ea;
                border-width: 11px;
                margin-top: -11px;
            }
            &:after {
                border-color: transparent;
                border-right-color: $background;
                border-width: 10px;
                margin-top: -10px;
            }
        }
    }
}
</style>
