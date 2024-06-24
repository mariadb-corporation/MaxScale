<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { MXS_OBJ_TYPES } from '@/constants'
import DagGraph from '@/components/visualizer/DagGraph.vue'
import html2canvas from 'html2canvas'

const { SERVICES, SERVERS, LISTENERS, MONITORS } = MXS_OBJ_TYPES
const SCALE_EXTENT = [0.01, 2]

const resourceTypes = [SERVICES, SERVERS, LISTENERS, MONITORS]

const { exportToJpeg, getPanAndZoomValues } = useHelpers()

const store = useStore()

const graphRef = ref(null)
const ctrDim = ref({})
const wrapperRef = ref(null)
const panAndZoom = ref({ x: 0, y: 0, k: 1 })
const isFitIntoView = ref(false)

const graphData = computed(() => {
  let data = []

  const dataMatrix = resourceTypes.reduce((acc, type) => {
    acc.push(store.state[type].all_objs)
    return acc
  }, [])
  dataMatrix.forEach((objects) =>
    objects.forEach((obj) => {
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

const zoomRatio = computed({
  get: () => panAndZoom.value.k,
  set: (v) => (panAndZoom.value.k = v),
})

watch(
  panAndZoom,
  (v) => {
    if (v.eventType && v.eventType == 'wheel') isFitIntoView.value = false
  },
  { deep: true }
)

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

function setZoom({ isFitIntoView: fitIntoView = false, transition = true, v } = {}) {
  isFitIntoView.value = fitIntoView
  const extent = graphRef.value.getExtent()
  panAndZoom.value = {
    ...getPanAndZoomValues({
      isFitIntoView: fitIntoView,
      extent,
      dim: ctrDim.value,
      scaleExtent: SCALE_EXTENT,
      paddingPct: 2,
      customZoom: v,
    }),
    transition,
  }
}
function fitIntoView({ transition = true } = {}) {
  setZoom({ isFitIntoView: true, transition })
}

function onRendered() {
  fitIntoView()
}

async function getCanvas() {
  return await html2canvas(graphRef.value.$el, { logging: false })
}

async function exportAsJpeg() {
  fitIntoView({ transition: false })
  exportToJpeg({ canvas: await getCanvas(), fileName: 'MaxScale_configuration_graph' })
}

onMounted(() => nextTick(() => setCtrDim()))
</script>

<template>
  <VCard ref="wrapperRef" flat border class="fill-height graph-card" v-resize-observer="setCtrDim">
    <portal to="view-header__right--append">
      <ZoomController
        :zoomRatio="zoomRatio"
        :isFitIntoView="isFitIntoView"
        :max-width="76"
        class="v-select--borderless mx-2"
        @update:zoomRatio="setZoom({ v: $event })"
        @update:isFitIntoView="fitIntoView({ transition: true })"
      />
      <TooltipBtn square variant="text" color="primary" density="compact" @click="exportAsJpeg">
        <template #btn-content><VIcon size="16" icon="$mdiDownload" /> </template>
        {{ $t('exportAsJpeg') }}
      </TooltipBtn>
    </portal>
    <DagGraph
      v-if="ctrDim.height && graphData.length"
      ref="graphRef"
      v-model:panAndZoom="panAndZoom"
      :data="graphData"
      :dim="ctrDim"
      :defNodeSize="{ width: 220, height: 100 }"
      :scaleExtent="SCALE_EXTENT"
      revert
      draggable
      :colorizingLinkFn="colorizingLinkFn"
      :handleRevertDiagonal="handleRevertDiagonal"
      @on-rendered.once="onRendered"
    >
      <template #graph-node-content="{ data: { node, nodeSize, onNodeResized, isDragging } }">
        <ConfNode
          :class="{ 'pointer-events--none': isDragging }"
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
