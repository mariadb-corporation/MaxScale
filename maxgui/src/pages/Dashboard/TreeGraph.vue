<template>
    <div
        class="tree-graph-container"
        :style="{
            width: dim.width + 'px',
            height: dim.height + 'px',
        }"
    >
        <svg ref="svg" class="tree-graph" />
        <div
            class="node-rect-wrapper fill-height"
            :style="{
                top: 0,
                height: 0,
                width: 0,
                position: 'absolute',
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
export default {
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
        }
    },
    computed: {
        scrollBarThickness() {
            return this.$help.getScrollbarWidth()
        },
        svgDim() {
            return {
                width: this.dim.width - this.scrollBarThickness,
                height: this.dim.height - this.scrollBarThickness,
            }
        },
        // return the width/height of tree content after subtracting margin)
        treeDim() {
            const { top, right, bottom, left } = this.layout.margin
            return {
                width: this.svgDim.width - left - right,
                height: this.svgDim.height - top - bottom,
            }
        },
        treeLayout() {
            // create a tree layout and assigns the size of the tree
            return tree().size([this.treeDim.height, this.treeDim.width])
        },
        root() {
            let root = hierarchy(this.data) //  compute a hierarchical layout
            // vertically center root node
            root.x0 = this.treeDim.height / 2
            root.y0 = 0
            return root
        },
    },
    watch: {
        data: {
            deep: true,
            async handler() {
                await this.update(this.root)
            },
        },
    },
    async mounted() {
        this.initSvg()
        await this.update(this.root)
    },

    methods: {
        initSvg() {
            // select svg to append a `g` element and that element to the top left margin
            this.svg = d3Select(this.$refs.svg)
                .attr('width', this.svgDim.width)
                .attr('height', this.svgDim.height)
                .append('g')
                .attr('id', 'node-group')
                .attr(
                    'transform',
                    'translate(' + this.layout.margin.left + ',' + this.layout.margin.top + ')'
                )
        },
        /**
         * Creates a curved (diagonal) path from source node to the destination nodes
         * @param {Object} src - hierarchy d3 source node
         * @param {Object} dest - hierarchy d3 destination node
         */
        diagonal(src, dest) {
            // draw link start at the border of source circle to border of the destination circle
            const srcY = src.y - this.circleRadius
            const destY = dest.y + this.circleRadius
            let path = `M ${srcY} ${src.x}
            C ${(srcY + destY) / 2} ${src.x},
              ${(srcY + destY) / 2} ${dest.x},
              ${destY} ${dest.x}`
            return path
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
         * Delete rectangular node div when node is toggled. The div is deleted by removing
         * key in nodeRectPosMap.
         * @param {Array} nodes - hierarchy d3 nodes
         */
        deleteNodeRect(nodes) {
            nodes.forEach(node => {
                this.$delete(this.nodeRectPosMap, node.id)
                if (node.children) this.deleteNodeRect(node.children)
            })
        },
        /**
         * Toggle node on click.
         * @param {Object} node - hierarchy d3 node
         */
        async onNodeClick(node) {
            if (node.children) {
                //collapse
                node._children = node.children
                node.children = null
                this.deleteNodeRect(node._children)
            } else {
                // expand
                node.children = node._children
                node._children = null
            }
            await this.update(node)
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
                .on('click', async (e, n) => {
                    e.stopPropagation()
                    await this.onNodeClick(n)
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
                    return d._children ? d.data.stroke : 'transparent'
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
        renderNodeRect() {
            this.nodeRectPosMap = this.getNodeRectPos()
        },
        getNodeRectPos() {
            let nodeRectPosMap = {}
            Array.from(this.$refs.svg.getElementsByClassName('node')).forEach(ele => {
                const nodeId = ele.getAttribute('data-node-id')
                let transform = ele.getAttribute('transform')
                let pos = {}
                transform.split(',').forEach((part, i) => {
                    let lat = part.replace(/[^\d.]/g, '')
                    let v = Math.round(Number(lat))
                    // offset 10 for rect arrow
                    if (i === 0) pos.left = v + this.circleRadius + 10
                    else pos.top = v
                })
                nodeRectPosMap[nodeId] = pos
            })
            return nodeRectPosMap
        },
        /**
         * Update node
         * @param {Object} srcNode - hierarchy d3 node to be updated
         */
        async update(srcNode) {
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
            await this.$help.delay(this.duration).then(() => this.renderNodeRect())
        },
    },
}
</script>

<style lang="scss" scoped>
.tree-graph-container {
    position: relative;
    overflow: auto;
}
::v-deep.tree-graph {
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
.node-rect {
    width: 276px;
    min-height: 50px;
    max-height: 80px;
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
</style>
