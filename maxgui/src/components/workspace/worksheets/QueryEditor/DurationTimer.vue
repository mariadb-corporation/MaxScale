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
const props = defineProps({
  start: { type: Number, required: true }, // in ms
  execTime: { type: Number, required: true }, // in seconds
  end: { type: Number, required: true }, // in ms
})

const count = ref(0)
const isRunning = computed(() => props.start && !props.end)
const elapsedTime = computed(() =>
  isRunning.value ? 0 : parseFloat(((props.end - props.start) / 1000).toFixed(4))
)
const latency = computed(() =>
  elapsedTime.value ? Math.abs(elapsedTime.value - props.execTime).toFixed(4) : 0
)

watch(
  isRunning,
  (v) => {
    if (v) updateCount()
  },
  { immediate: true }
)

watch(
  () => props.end,
  (v) => {
    // reset
    if (v) count.value = 0
  },
  { immediate: true }
)

function updateCount() {
  if (!isRunning.value) return
  const now = new Date().valueOf()
  count.value = parseFloat(((now - props.start) / 1000).toFixed(4))
  requestAnimationFrame(updateCount)
}
</script>

<template>
  <div class="d-inline-flex text-truncate">
    <div data-test="exe-time">
      <span class="font-weight-bold">{{ $t('exeTime') }}:</span>
      {{ isRunning ? 'N/A' : `${execTime} sec` }}
    </div>
    <div class="ml-2" data-test="latency-time">
      <span class="font-weight-bold">{{ $t('latency') }}:</span>
      {{ isRunning ? 'N/A' : `${latency} sec` }}
    </div>
    <div class="ml-2" data-test="total-time">
      <span class="font-weight-bold mr-1"> {{ $t('total') }}:</span>
      {{ isRunning ? Math.round(count) : elapsedTime }} sec
    </div>
  </div>
</template>
