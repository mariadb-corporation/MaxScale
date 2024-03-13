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

/**
 * Events
 * get-graph-ctr(SVGElement): graph-ctr <g/> element
 * on-board-contextmenu(Event)
 */
import { select as d3Select } from 'd3-selection'
import 'd3-transition'
import { zoom, zoomIdentity } from 'd3-zoom'

const props = defineProps({
  modelValue: { type: Object, required: true },
  dim: { type: Object, required: true },
  graphDim: { type: Object, default: () => ({ width: 0, height: 0 }) },
  scaleExtent: { type: Array, default: () => [0.25, 2] },
})
const emit = defineEmits(['update:modelValue', 'get-graph-ctr', 'on-board-contextmenu'])

const {
  lodash: { isEqual },
} = useHelpers()

let svgRef = ref(null)
let d3Svg = null

const panAndZoom = computed({
  get: () => props.modelValue,
  set: (v) => emit('update:modelValue', v),
})
const style = computed(() => {
  const { x, y, k, transition } = panAndZoom.value
  return {
    transform: `translate(${x}px, ${y}px) scale(${k})`,
    transition: `all ${transition ? 0.3 : 0}s ease`,
  }
})

watch(
  () => props.graphDim,
  (v, oV) => {
    if (!isEqual(v, oV)) centerGraph()
  },
  { deep: true }
)
watch(
  panAndZoom,
  (v, oV) => {
    if (!isEqual(v, oV)) applyZoom(v)
  },
  { deep: true }
)
onMounted(() => initSvg())

function initSvg() {
  d3Svg = d3Select(svgRef.value)
  d3Svg
    .call(
      zoom()
        .scaleExtent(props.scaleExtent)
        .on('zoom', (e) => {
          const { x, y, k } = e.transform
          panAndZoom.value = { x, y, k, eventType: e.sourceEvent.type }
        })
    )
    .on('dblclick.zoom', null)
  centerGraph()
  emit('get-graph-ctr', d3Svg.select('g#graph-ctr'))
}
// Vertically and horizontally Center graph
function centerGraph() {
  const x = (props.dim.width - props.graphDim.width) / 2,
    y = (props.dim.height - props.graphDim.height) / 2,
    k = 1
  panAndZoom.value = { ...panAndZoom.value, x, y, k }
}
function applyZoom(v) {
  d3Svg.call(zoom().transform, zoomIdentity.translate(v.x, v.y).scale(v.k))
}
</script>

<template>
  <div class="svg-graph-board-ctr fill-height w-100 relative overflow-hidden">
    <VIcon
      class="svg-grid-bg absolute fill-height w-100"
      color="card-border-color"
      icon="mxs:gridBg"
    />
    <svg
      ref="svgRef"
      class="svg-graph-board relative"
      :width="dim.width"
      height="100%"
      @contextmenu.prevent="$emit('on-board-contextmenu', $event)"
    >
      <g id="graph-ctr" :style="style" />
    </svg>
    <slot name="append" :data="{ style }" />
  </div>
</template>

<style lang="scss" scoped>
.svg-graph-board-ctr {
  .svg-grid-bg {
    z-index: 1;
    pointer-events: none;
    background: transparent;
    left: 0;
  }
  .svg-graph-board {
    left: 0;
    z-index: 2;
  }
}
</style>
