<template>
    <div
        :style="{
            width: dim.width + 'px',
            height: dim.height + 'px',
        }"
    >
        <svg ref="svg" class="tree-graph" />
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
            duration: 500,
            layout: {
                link: {
                    length: 320,
                },
                margin: { top: 20, right: 20, bottom: 20, left: 48 },
            },
            circleRadius: 7,
            svg: null, // svg obj
        }
    },
    computed: {
        // return the width/height of tree content after subtracting margin)
        treeDim() {
            const { top, right, bottom, left } = this.layout.margin
            return { width: this.dim.width - left - right, height: this.dim.height - top - bottom }
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
            handler() {
                this.update(this.root)
            },
        },
    },
    mounted() {
        this.initSvg()
        this.update(this.root)
    },

    methods: {
        initSvg() {
            // select svg to append a `g` element and that element to the top left margin
            this.svg = d3Select(this.$refs.svg)
                .attr('width', this.dim.width)
                .attr('height', this.dim.height)
                .append('g')
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
                // use srcNode.y as x for translate to make horizontal graph
                .attr('transform', () => 'translate(' + srcNode.y0 + ',' + srcNode.x0 + ')')
                .on('click', (e, n) => this.onNodeClick(n))
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
                .attr('r', this.circleRadius)
                .style('fill', d => {
                    return d._children ? d.data.stroke : 'transparent'
                })
                .style('stroke', d => d.data.stroke)
                .attr('cursor', d => (d.children || d._children ? 'pointer' : ''))

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
        },
    },
}
</script>

<style lang="scss" scoped>
::v-deep.tree-graph {
    .link {
        fill: none;
        stroke: #e7eef1;
        stroke-width: 1.5px;
    }
}
</style>
