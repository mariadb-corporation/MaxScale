<template>
    <div class="graph-nodes-ctr">
        <div
            v-for="node in nodes"
            ref="graphNode"
            :key="node.data.id"
            :class="`graph-node ${nodeClass} ${draggable ? 'move' : ''}`"
            :node_id="node.data.id"
            :style="{
                top: `${node.y}px`,
                left: `${node.x}px`,
                ...nodeStyle,
                zIndex: draggingNodeId === node.data.id ? 4 : 3,
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
            <slot :node="node" />
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
 */
export default {
    name: 'graph-nodes',
    props: {
        nodes: {
            type: Array,
            validator(arr) {
                const isArray = Array.isArray(arr)
                if (isArray && arr.length) {
                    const node = arr[0]
                    return ['x', 'y', 'data'].every(key => key in node) && node.data.id
                }
                return isArray
            },
            required: true,
        },
        nodeStyle: { type: Object, default: () => ({}) },
        nodeClass: { type: String, default: '' },
        draggable: { type: Boolean, default: false },
        draggingNodeId: { type: String, default: '' },
    },
    beforeDestroy() {
        if (this.draggable) this.rmMouseUpEvt()
    },
    methods: {
        /**
         * Method to be used by parent component.
         */
        getHeightMap() {
            const graphNode = this.$typy(this.$refs, 'graphNode').safeArray
            let heightMap = {}
            graphNode.forEach(node => (heightMap[node.getAttribute('node_id')] = node.clientHeight))
            return heightMap
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
