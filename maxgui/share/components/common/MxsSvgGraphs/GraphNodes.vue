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
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * Events
 * drag-start({ e, node })
 * drag({ e, node, diffX, diffY })
 * drag-end({ e, node })
 * node-size-map(obj): size of nodes, keyed by node id
 * dblclick(node)
 * on-node-contextmenu({e:Event, node:object})
 * click(node)
 * click-out-side
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
        dblclick: { type: Boolean, default: false },
        contextmenu: { type: Boolean, default: false },
        click: { type: Boolean, default: false },
        clickOutside: { type: Boolean, default: false },
        clickedNodeId: { type: String, default: '' },
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
        clickedGraphNodeId: {
            get() {
                return this.clickedNodeId
            },
            set(v) {
                this.$emit('update:clickedNodeId', v)
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
        clickOutside: {
            immediate: true,
            handler(v) {
                if (v) document.addEventListener('click', this.onClickOutside)
                else document.removeEventListener('click', this.onClickOutside)
            },
        },
    },
    created() {
        if (this.draggable) this.setDefDraggingStates()
    },
    beforeDestroy() {
        if (this.clickOutside) document.removeEventListener('click', this.onClickOutside)
    },
    methods: {
        handleAddEvents(node) {
            let events = {}
            if (this.draggable) events.mousedown = e => this.dragStart({ e, node })
            if (this.hoverable && !this.draggingStates.isDragging) {
                events.mouseenter = e => this.$emit('mouseenter', { e, node })
                events.mouseleave = e => this.$emit('mouseleave', { e, node })
            }
            if (this.dblclick)
                events.dblclick = e => {
                    this.isDblclick = true
                    clearTimeout(this.dblclickTimeout)
                    this.dblclickTimeout = setTimeout(() => (this.isDblclick = false), 200)
                    this.resetClickedNodeId()
                    e.stopPropagation()
                    this.$emit('dblclick', node)
                }
            if (this.contextmenu)
                events.contextmenu = e => {
                    this.resetClickedNodeId()
                    e.preventDefault()
                    this.$emit('on-node-contextmenu', { e, node })
                }
            if (this.click)
                events.click = e => {
                    clearTimeout(this.clickTimeout)
                    this.clickTimeout = setTimeout(() => {
                        if (!this.isDblclick) {
                            e.stopPropagation()
                            this.clickedGraphNodeId = node.id
                            this.$emit('click', node)
                        }
                    }, 200)
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
                const { width, height } = this.getNodeEleSize(node)
                nodeSizeMap[node.getAttribute('node_id')] = { width, height }
            })
            this.nodeSizeMap = nodeSizeMap
        },
        getNodeSize(id) {
            return this.nodeSizeMap[id] || this.defNodeSize
        },
        /**
         * Calculates the size of an HTML element
         * The returned dimensions represent the value when the zoom ratio is 1.
         * @param {HTMLElement} node
         * @returns {{ width: number, height: number }}
         */
        getNodeEleSize(node) {
            const { width, height } = node.getBoundingClientRect()
            return { width: width / this.boardZoom, height: height / this.boardZoom }
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
         * @param {string} nodeId - Id of the node that was resized.
         */
        onNodeResized(nodeId) {
            // Run with doubleRAF to make sure getBoundingClientRect return accurate dim
            this.$helpers.doubleRAF(() => {
                const nodeEle = this.$typy(this.$refs, 'graphNode').safeArray.find(
                    n => n.getAttribute('node_id') === nodeId
                )
                if (nodeEle) {
                    const { width, height } = this.getNodeEleSize(nodeEle)
                    this.$set(this.nodeSizeMap, nodeId, { width, height })
                }
            })
        },
        addDragEvents(node) {
            /**
             * The handlers for mousemove and mouseup events are arrow functions which can't
             * be removed as they aren't attached to any variables.
             * This stores them to dragEvent and dragEndEvent so they can be later removed.
             */
            this.dragEvent = e => this.drag({ e, node })
            this.dragEndEvent = e => this.dragEnd({ e, node })
            document.addEventListener('mousemove', this.dragEvent)
            document.addEventListener('mouseup', this.dragEndEvent)
        },
        rmDragEvents() {
            document.removeEventListener('mousemove', this.dragEvent)
            document.removeEventListener('mouseup', this.dragEndEvent)
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
            this.resetClickedNodeId()
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
        resetClickedNodeId() {
            this.clickedGraphNodeId = ''
        },
        onClickOutside() {
            if (this.clickedGraphNodeId) {
                this.$emit('click-out-side')
                this.resetClickedNodeId()
            }
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
