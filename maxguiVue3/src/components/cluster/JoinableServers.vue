<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Sortable from 'sortablejs'
import ServerNode from '@/components/cluster/ServerNode.vue'

defineOptions({ inheritAttrs: false })
const props = defineProps({
  data: { type: Array, required: true },
  draggableGroup: { type: Object, required: true },
  cloneClass: { type: String, required: true },
  dim: { type: Object, required: true },
  draggable: { type: Boolean, default: false },
})

const emit = defineEmits(['on-drag-start', 'on-dragging', 'on-drag-end'])

const vDraggable = {
  beforeMount: (el) => {
    const options = {
      /**
       *  enable swap, so the onMove event can be triggered while the
       *  joinable server is dragged on top of the target node
       */
      swap: true,
      group: props.draggableGroup,
      draggable: '.joinable-node--draggable',
      ghostClass: 'node-ghost',
      animation: 0,
      forceFallback: true,
      fallbackClass: props.cloneClass,
      onStart: (e) => emit('on-drag-start', e),
      onMove: (e) => {
        emit('on-dragging', e)
        return false // cancel drop
      },
      onEnd: (e) => emit('on-drag-end', e),
    }
    Sortable.create(el, options)
  },
}
</script>

<template>
  <VCard flat class="joinable-card absolute mxs-color-helper all-border-warning">
    <div class="d-flex align-center justify-center flex-row px-3 py-1 bg-warning">
      <span class="card-title font-weight-medium text-uppercase text-white">
        {{ $t('rejoinableNodes') }}
      </span>
    </div>
    <VDivider />
    <div
      v-draggable="draggable"
      class="relative overflow-y-auto px-3 pt-2 pb-1"
      :style="{ maxHeight: `${dim.height / 1.5}px` }"
    >
      <ServerNode
        v-for="node in data"
        :key="node.id"
        :node="node"
        :node_id="node.id"
        class="mb-2"
        :class="{ 'joinable-node--draggable move': draggable }"
        v-bind="$attrs"
      />
    </div>
  </VCard>
</template>

<style lang="scss" scoped>
.joinable-card {
  top: 0;
  right: 0;
  z-index: 5;
  .card-title {
    font-size: 0.875rem;
  }
}
.node-ghost {
  background: colors.$tr-hovered-color;
  opacity: 0.6;
}
.drag-node-clone {
  opacity: 1 !important;
}
</style>
