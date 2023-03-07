<template>
    <div class="graph-nodes-ctr">
        <div
            v-for="node in nodes"
            ref="graphNode"
            :key="node.id"
            :class="`graph-node ${nodeClass} ${draggable ? 'move' : ''}`"
            :node_id="node.id"
            :style="{
                ...getPos(node),
                ...nodeStyle,
                zIndex: draggingNodeId === node.id ? 4 : 3,
                /**
                 * Graph nodes will be rendered but they won't be visible.
                 * It's done this way so node size can be calculated dynamically
                 */
                visibility: $typy(posMap).isEmptyObject ? 'hidden' : 'visible',
            }"
            v-on="
                draggable
                    ? {
                          mousedown: e => dragStart({ e, node }),
                          mousemove: e => drag({ e, node }),
                      }
                    : null
            "
        >
            <slot :node="node" :changeNodeSize="changeNodeSize" />
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
 * drag({ e, node })
 * drag-end(e)
 * node-size-map(obj): size of nodes, keyed by node id
 */
export default {
    name: 'graph-nodes',
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
        posMap: { type: Object, default: () => ({}) },
        nodeStyle: { type: Object, default: () => ({}) },
        nodeClass: { type: String, default: '' },
        draggable: { type: Boolean, default: false },
        draggingNodeId: { type: String, default: '' },
        dynNode: { type: Boolean, default: false },
    },
    data() {
        return {
            dynNodeSizeMap: {},
        }
    },
    computed: {
        nodeIds() {
            return this.nodes.map(n => n.id)
        },
    },
    watch: {
        nodeIds: {
            deep: true,
            immediate: true,
            handler(v, oV) {
                // compute node height after nodes are rendered
                if (this.dynNode && !this.$helpers.lodash.isEqual(v, oV))
                    this.$helpers.doubleRAF(() => this.setNodeSizeMap())
            },
        },
        dynNodeSizeMap: {
            deep: true,
            immediate: true,
            handler(v) {
                this.$emit('node-size-map', v)
            },
        },
    },
    beforeDestroy() {
        if (this.draggable) this.rmMouseUpEvt()
    },
    methods: {
        setNodeSizeMap() {
            const graphNode = this.$typy(this.$refs, 'graphNode').safeArray
            let nodeSizeMap = {}
            graphNode.forEach(node => {
                const { width, height } = node.getBoundingClientRect()
                nodeSizeMap[node.getAttribute('node_id')] = { width, height }
            })
            if (!this.$helpers.lodash.isEqual(this.dynNodeSizeMap, nodeSizeMap))
                this.dynNodeSizeMap = nodeSizeMap
        },
        getPos(node) {
            const { x = 0, y = 0 } = this.$typy(this.posMap, `[${node.id}]`).safeObjectOrEmpty
            return {
                left: `${x}px`,
                top: `${y}px`,
            }
        },
        changeNodeSize(node) {
            this.$helpers.doubleRAF(() => {
                const nodeEle = this.$typy(this.$refs, 'graphNode').safeArray.find(
                    n => n.getAttribute('node_id') === node.id
                )
                if (nodeEle) {
                    const { width, height } = nodeEle.getBoundingClientRect()
                    this.$set(this.dynNodeSizeMap, node.id, { width, height })
                }
            })
        },
        addMouseUpEvt() {
            document.addEventListener('mouseup', this.dragEnd)
        },
        rmMouseUpEvt() {
            document.removeEventListener('mouseup', this.dragEnd)
        },
        dragStart(param) {
            this.$emit('drag-start', param)
            this.addMouseUpEvt()
        },
        drag(param) {
            this.$emit('drag', param)
        },
        dragEnd(param) {
            this.$emit('drag-end', param)
            this.rmMouseUpEvt()
        },
    },
}
</script>

<style lang="scss" scoped>
.graph-nodes-ctr {
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
