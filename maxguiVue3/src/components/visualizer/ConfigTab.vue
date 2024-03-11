<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { MXS_OBJ_TYPES } from '@/constants'
import DagGraph from '@/components/visualizer/DagGraph.vue'

const store = useStore()
const config_graph_data = computed(() => store.state.visualization.config_graph_data)

let ctrDim = ref({})
let wrapperRef = ref(null)

function setCtrDim() {
  const { clientWidth, clientHeight } = wrapperRef.value.$el
  ctrDim.value = { width: clientWidth, height: clientHeight }
}

function colorizingLinkFn({ source, target }) {
  const sourceType = source.data.type
  const targetType = target.data.type
  const { SERVICES, SERVERS, MONITORS, LISTENERS } = MXS_OBJ_TYPES
  switch (sourceType) {
    case MONITORS:
      if (targetType === SERVERS || targetType === SERVERS) return '#0E9BC0'
      else if (targetType === SERVICES) return '#7dd012'
      break
    case SERVERS:
      if (targetType === SERVICES) return '#7dd012'
      break
    case SERVICES:
      if (targetType === LISTENERS) return '#424f62'
      else if (targetType === SERVICES) return '#7dd012'
  }
}
function handleRevertDiagonal({ source, target }) {
  const sourceType = source.data.type
  const targetType = target.data.type
  const { SERVICES, SERVERS, MONITORS, LISTENERS } = MXS_OBJ_TYPES
  switch (sourceType) {
    case MONITORS:
    case SERVERS:
      if (targetType === SERVICES) return true
      break
    case SERVICES:
      if (targetType === LISTENERS || targetType === SERVICES) return true
      break
  }
  return false
}

onMounted(() => nextTick(() => setCtrDim()))
</script>

<template>
  <VCard ref="wrapperRef" flat border class="fill-height graph-card" v-resize.quiet="setCtrDim">
    <DagGraph
      v-if="ctrDim.height && config_graph_data.length"
      :data="config_graph_data"
      :dim="ctrDim"
      :defNodeSize="{ width: 220, height: 100 }"
      revert
      draggable
      :colorizingLinkFn="colorizingLinkFn"
      :handleRevertDiagonal="handleRevertDiagonal"
    >
      <template #graph-node-content="{ data: { node, nodeSize, onNodeResized, isDragging } }">
        <ConfNode
          :class="{ 'no-pointerEvent': isDragging }"
          :style="{ minWidth: '220px', maxWidth: '250px' }"
          :node="node"
          :nodeWidth="nodeSize.width"
          :onNodeResized="onNodeResized"
          showFiltersInService
        />
      </template>
    </DagGraph>
  </VCard>
</template>
