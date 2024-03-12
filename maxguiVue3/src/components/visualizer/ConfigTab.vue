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

const { SERVICES, SERVERS, LISTENERS, MONITORS } = MXS_OBJ_TYPES

const store = useStore()
const graphData = computed(() => {
  const {
    services: { all_services },
    servers: { all_servers },
    monitors: { all_monitors },
    listeners: { all_listeners },
  } = store.state
  let data = []
  const rsrcData = [all_services, all_servers, all_listeners, all_monitors]
  rsrcData.forEach((rsrc) =>
    rsrc.forEach((obj) => {
      const { id, type, relationships } = obj
      let node = { id, type, nodeData: obj, parentIds: [] }
      /**
       * DAG graph requires root nodes.
       * With current data from API, accurate links between nodes can only be found by
       * checking the relationships data of a service. So monitors are root nodes here.
       * This adds parent node ids for services, servers and listeners node to create links except
       * monitors, as the links between monitors and servers or monitors and services are created
       * already. This is an intention to prevent circular reference.
       */
      let relationshipTypes = []
      switch (type) {
        case SERVICES:
          // a service can also target services or monitors
          relationshipTypes = [SERVERS, SERVICES, MONITORS]
          break
        case SERVERS:
          relationshipTypes = [MONITORS]
          break
        case LISTENERS:
          relationshipTypes = [SERVICES]
          break
      }
      Object.keys(relationships).forEach((key) => {
        if (relationshipTypes.includes(key))
          relationships[key].data.forEach((n) => {
            node.parentIds.push(n.id) // create links
          })
      })
      data.push(node)
    })
  )
  return data
})

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
      v-if="ctrDim.height && graphData.length"
      :data="graphData"
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
