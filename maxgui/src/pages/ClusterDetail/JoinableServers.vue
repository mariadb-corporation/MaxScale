<template>
    <v-card class="joinable-card color border-all-warning">
        <div class="d-flex align-center justify-center flex-row px-3 py-1 color bg-warning">
            <span class="card-title font-weight-medium text-uppercase color text-background">
                {{ $t('rejoinableNodes') }}
            </span>
        </div>
        <v-divider />
        <div
            v-draggable
            class="joinable-node-wrapper px-3 pt-2"
            :style="{
                maxHeight: `${dim.height / 1.5}px`,
            }"
        >
            <!-- TODO: Extend server-node to allow changing the border color -->
            <server-node
                v-for="node in data"
                :key="node.id"
                :node="node"
                :droppableTargets="droppableTargets"
                :bodyWrapperClass="bodyWrapperClass"
                :node_id="node.id"
                class="mb-2 move"
                v-on="$listeners"
            />
        </div>
    </v-card>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-01-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
This component emits the following events
@on-drag-start: e: Event. Starts dragging a node
@on-dragging: e: Event, callback: (v: bool):void. Move a node. If the callback returns true, it accepts the new position
@on-drag-end: e: Event. Dragging ended
*/
import Sortable from 'sortablejs'
import ServerNode from './ServerNode'
export default {
    name: 'joinable-servers',
    components: {
        'server-node': ServerNode,
    },
    directives: {
        draggable: {
            bind(el, binding, vnode) {
                const options = {
                    /**
                     *  enable swap, so the onMove event can be triggered while the
                     *  joinable server is dragged on top of the target node
                     */
                    swap: true,
                    group: vnode.context.$props.draggableGroup,
                    ghostClass: 'node-ghost',
                    animation: 0,
                    forceFallback: true,
                    fallbackClass: vnode.context.$props.cloneClass,
                    onStart: e => vnode.context.$emit('on-drag-start', e),
                    onMove: e => {
                        vnode.context.$emit('on-dragging', e)
                        return false // cancel drop
                    },
                    onEnd: e => vnode.context.$emit('on-drag-end', e),
                }
                Sortable.create(el, options)
            },
        },
    },
    props: {
        data: { type: Array, required: true },
        draggableGroup: { type: Object, required: true },
        bodyWrapperClass: { type: String, required: true },
        cloneClass: { type: String, required: true },
        droppableTargets: { type: Array, required: true },
        dim: { type: Object, required: true },
    },
}
</script>

<style lang="scss" scoped>
.joinable-card {
    position: absolute;
    top: 0;
    right: 0;
    z-index: 5;
    .card-title {
        font-size: 0.875rem;
    }
    .joinable-node-wrapper {
        position: relative;
        overflow-y: auto;
    }
}
.node-ghost {
    background: #f2fcff !important;
    opacity: 0.6;
}
.drag-node-clone {
    opacity: 1 !important;
}
</style>
