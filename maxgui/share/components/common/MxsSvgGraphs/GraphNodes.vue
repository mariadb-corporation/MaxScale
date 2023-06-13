<template>
    <div class="mxs-svg-graph-nodes-ctr">
        <div
            v-for="node in nodes"
            ref="graphNode"
            :key="node.id"
            class="graph-node"
            :class="{ move: draggable, 'no-userSelect': draggingStates.isDragging }"
            :node_id="node.id"
            :style="{
                ...getPosStyle(node.id),
                ...nodeStyle,
                ...getNodeSizeStyle(node.id),
                zIndex: draggingStates.draggingNodeId === node.id ? 4 : 3,
            }"
            v-on="handleAddEvents(node)"
        >
            <slot
                :data="{
                    node,
                    nodeSize: getNodeSize(node.id),
                    onNodeResized,
                    isDragging: draggingStates.isDragging,
                }"
            />
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
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * Events
 * drag-start({ e, node })
 * drag({ e, node, diffX, diffY })
 * drag-end(e)
 * node-size-map(obj): size of nodes, keyed by node id
 */
export default {
    name: 'mxs-svg-graph-nodes',
    props: {
        nodes: {
            type: Array,
            validator(arr) {
                const isArray = Array.isArray(arr)
                if (isArray && arr.length) return 'id' in arr[0]
                return isArray
            },
            required: true,
        },
        coordMap: { type: Object, default: () => ({}) }, //sync
        nodeStyle: { type: Object, default: () => ({}) },
        defNodeSize: { type: Object, required: true },
        draggable: { type: Boolean, default: false },
        autoWidth: { type: Boolean, default: false },
        revertDrag: { type: Boolean, default: false },
        boardZoom: { type: Number, required: true },
        hoverable: { type: Boolean, default: false },
    },
    data() {
        return {
            nodeSizeMap: {},
            // states for dragging graph-node
            defDraggingStates: {
                isDragging: false,
                draggingNodeId: null,
                startCoord: null,
            },
            draggingStates: null,
        }
    },
    computed: {
        nodeIds() {
            return this.nodes.map(n => n.id)
        },
        nodeCoordMap: {
            get() {
                return this.coordMap
            },
            set(v) {
                this.$emit('update:coordMap', v)
            },
        },
    },
    watch: {
        nodeIds: {
            deep: true,
            immediate: true,
            handler(v, oV) {
                // compute node height after nodes are rendered
                if (!this.$helpers.lodash.isEqual(v, oV))
                    this.$helpers.doubleRAF(() => this.setNodeSizeMap())
            },
        },
        nodeSizeMap: {
            deep: true,
            handler(v) {
                this.$emit('node-size-map', v)
            },
        },
    },
    created() {
        if (this.draggable) this.setDefDraggingStates()
    },
    methods: {
        handleAddEvents(node) {
            let events = {}
            if (this.draggable)
                events = {
                    ...events,
                    mousedown: e => this.dragStart({ e, node }),
                }
            if (this.hoverable && !this.draggingStates.isDragging)
                events = {
                    ...events,
                    mouseenter: e => this.$emit('mouseenter', { e, node }),
                    mouseleave: e => this.$emit('mouseleave', { e, node }),
                }
            return events
        },
        setDefDraggingStates() {
            this.draggingStates = this.$helpers.lodash.cloneDeep(this.defDraggingStates)
        },
        setNodeSizeMap() {
            const graphNode = this.$typy(this.$refs, 'graphNode').safeArray
            let nodeSizeMap = {}
            graphNode.forEach(node => {
                const { width, height } = node.getBoundingClientRect()
                nodeSizeMap[node.getAttribute('node_id')] = { width, height }
            })
            this.nodeSizeMap = nodeSizeMap
        },
        getNodeSize(id) {
            return this.nodeSizeMap[id] || this.defNodeSize
        },
        getPosStyle(id) {
            const { x = 0, y = 0 } = this.nodeCoordMap[id] || {}
            const { width, height } = this.getNodeSize(id)
            // center rectangular nodes
            return {
                left: `${x - width / 2}px`,
                top: `${y - height / 2}px`,
            }
        },
        getNodeSizeStyle(id) {
            const { width } = this.getNodeSize(id)
            return {
                width: this.autoWidth ? 'unset' : `${width}px`,
                height: 'unset',
            }
        },
        /**
         * Handles the event when a node is resized.
         * @param {Object} node - The node object that was resized.
         */
        onNodeResized(node) {
            // Run with doubleRAF to make sure getBoundingClientRect return accurate dim
            this.$helpers.doubleRAF(() => {
                const nodeEle = this.$typy(this.$refs, 'graphNode').safeArray.find(
                    n => n.getAttribute('node_id') === node.id
                )
                if (nodeEle) {
                    const { width, height } = nodeEle.getBoundingClientRect()
                    this.$set(this.nodeSizeMap, node.id, { width, height })
                }
            })
        },
        addDragEvents(node) {
            document.addEventListener('mousemove', e => this.drag({ e, node }))
            document.addEventListener('mouseup', this.dragEnd)
        },
        rmDragEvents() {
            document.removeEventListener('mousemove', this.drag)
            document.removeEventListener('mouseup', this.dragEnd)
        },
        dragStart({ e, node }) {
            this.draggingStates = {
                ...this.draggingStates,
                draggingNodeId: node.id,
                startCoord: { x: e.clientX, y: e.clientY },
            }
            this.$emit('drag-start', { e, node })
            this.addDragEvents(node)
        },
        drag({ e, node }) {
            e.preventDefault()
            const { startCoord, draggingNodeId } = this.draggingStates
            if (startCoord && draggingNodeId === node.id) {
                const diffPos = { x: e.clientX - startCoord.x, y: e.clientY - startCoord.y }
                // calc offset position
                let diffX = diffPos.x / this.boardZoom,
                    diffY = diffPos.y / this.boardZoom
                // update startCoord
                this.draggingStates = {
                    ...this.draggingStates,
                    isDragging: true,
                    startCoord: { x: e.clientX, y: e.clientY },
                }

                //  graph is reverted, so minus offset
                if (this.revertDrag) {
                    diffX = -diffX
                    diffY = -diffY
                }
                const coord = this.nodeCoordMap[draggingNodeId]
                this.$set(this.nodeCoordMap, draggingNodeId, {
                    x: coord.x + diffX,
                    y: coord.y + diffY,
                })
                this.$emit('drag', { e, node, diffX, diffY })
            }
        },
        dragEnd(param) {
            this.$emit('drag-end', param)
            this.rmDragEvents()
            this.setDefDraggingStates()
        },
    },
}
</script>

<style lang="scss" scoped>
.mxs-svg-graph-nodes-ctr {
    top: 0;
    height: 0;
    width: 0;
    position: absolute;
    z-index: 3;
    .graph-node {
        position: absolute;
        background: transparent;
    }
}
</style>
