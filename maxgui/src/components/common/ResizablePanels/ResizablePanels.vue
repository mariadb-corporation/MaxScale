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
import ResizeHandle from '@/components/common/ResizablePanels/ResizeHandle.vue'
import ResizablePane from '@/components/common/ResizablePanels/ResizablePane.vue'

const props = defineProps({
  modelValue: { type: Number, required: true },
  boundary: { type: Number, required: true }, // boundary value in pixel unit
  minPercent: { type: Number, default: 0 },
  maxPercent: { type: Number, default: 100 },
  /**
   * The value for each prop must meet the condition to take effect
   * deactivatedMinPctZone > minPercent
   * deactivatedMaxPctZone < maxPercent
   * If the props are not used, it will use minPercent/maxPercent as the value
   */
  deactivatedMinPctZone: { type: Number },
  deactivatedMaxPctZone: { type: Number },
  split: {
    type: String,
    validator(value) {
      return ['vert', 'horiz'].indexOf(value) !== -1
    },
    required: true,
  },
  disable: { type: Boolean, default: false },
  /**
   * Let the resize action continue but the value props is stopped at minPercent or maxPercent.
   * This emits @resizing with value beyond minPercent/maxPercent threshold
   */
  progress: { type: Boolean, default: false },
})
const emit = defineEmits(['update:modelValue', 'resizing'])

const { getAppEle } = useHelpers()

const active = ref(false)
const currPct = ref(0)
const startPoint = ref(0)
const initialValueInPx = ref(0)

const isVertSplit = computed(() => props.split === 'vert')
const panePosType = computed(() => (isVertSplit.value ? 'width' : 'height'))
const resizerPosType = computed(() => (isVertSplit.value ? 'left' : 'top'))
const leftPanelPos = computed(() => ({ [panePosType.value]: `${currPct.value}%` }))
const rightPanelPos = computed(() => ({ [panePosType.value]: `${100 - currPct.value}%` }))
const resizingStyle = computed(() => ({
  pointerEvents: active.value ? 'all !important' : 'auto',
}))
const resizerStyle = computed(() => {
  const style = {
    [resizerPosType.value]: `${currPct.value}%`,
    ...resizingStyle.value,
  }
  if (props.disable) {
    style.cursor = 'unset'
    style.pointerEvents = 'none'
  }
  return style
})
const cursor = computed(() =>
  active.value ? (props.split === 'vert' ? 'col-resize' : 'row-resize') : ''
)
const deactMinPctZone = computed(() => props.deactivatedMinPctZone || props.minPercent)
const deactMaxPctZone = computed(() => props.deactivatedMaxPctZone || props.maxPercent)

watch(active, (v, oV) => {
  // active true when user resizing, false when finish it
  if (oV && !v) {
    // only emit when user finishes resizing
    emit('update:modelValue', currPct.value)
  }
  if (v) getAppEle().classList.add('user-select--none--all', 'pointer-events--none--all')
  else getAppEle().classList.remove('user-select--none--all', 'pointer-events--none--all')
})
watch(
  () => props.modelValue,
  (v) => {
    /* Set initial modelValue and update current percent when modelValue change. e.g when
     * modelValue is changed by the parent component
     */
    if (v !== currPct.value) currPct.value = v
  },
  { immediate: true }
)

function addEvents() {
  window.addEventListener('mousemove', onMouseMove)
  window.addEventListener('mouseup', onMouseUp)
}

function rmEvents() {
  window.removeEventListener('mousemove', onMouseMove)
  window.removeEventListener('mouseup', onMouseUp)
}

function onMouseDown(e) {
  initialValueInPx.value = (currPct.value * props.boundary) / 100
  startPoint.value = isVertSplit.value ? e.pageX : e.pageY
  addEvents()
  active.value = true
}

function onMouseUp() {
  rmEvents()
  active.value = false
}

function calCurrPct(offset) {
  return ((initialValueInPx.value + offset) / props.boundary) * 100
}

function isInThreshold(v) {
  return v <= props.minPercent && v >= props.maxPercent
}

/**
 * prevent currPct from having value beyond the minPercent threshold
 * currPct stops updating when the percent <= minPercent
 */
function handleBrakeOnMinPct(v) {
  if (v <= props.minPercent) currPct.value = props.minPercent
}

/**
 * prevent currPct from having value beyond the maxPercent threshold
 * currPct stops updating when the percent >= maxPercent
 */
function handleBrakeOnMaxPct(v) {
  if (v >= props.maxPercent) currPct.value = props.maxPercent
}

function onMouseMove(e) {
  const endpoint = isVertSplit.value ? e.pageX : e.pageY
  const offset = endpoint - startPoint.value
  const percent = calCurrPct(offset)

  // stop updating currPct on deactivated zone but continue if it reaches minPercent/maxPercent
  if (deactMinPctZone.value && percent <= deactMinPctZone.value) handleBrakeOnMinPct(percent)
  else if (deactMaxPctZone.value && percent >= deactMaxPctZone.value) handleBrakeOnMaxPct(percent)
  else if (isInThreshold(percent)) {
    handleBrakeOnMinPct(percent)
    handleBrakeOnMaxPct(percent)
  } else currPct.value = percent

  // emit event that returns value beyond the minPercent/maxPercent threshold
  if (props.progress) emit('resizing', calCurrPct(offset))
}
</script>

<template>
  <div class="resizable-panels fill-height pos--relative" :style="{ cursor, ...resizingStyle }">
    <ResizablePane isLeft :split="split" :style="leftPanelPos" data-test="pane-left">
      <slot name="pane-left" />
    </ResizablePane>
    <ResizablePane :split="split" :style="rightPanelPos" data-test="pane-right">
      <slot name="pane-right" />
    </ResizablePane>
    <ResizeHandle
      v-if="!disable"
      :style="resizerStyle"
      :split="split"
      :active="active"
      @mousedown="onMouseDown"
    />
    <div v-if="active" class="dragging-mask" />
  </div>
</template>
