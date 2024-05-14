<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
const props = defineProps({
  executionTime: { type: Number, required: true }, // in seconds
  startTime: { type: Number, required: true }, // in ms
  totalDuration: { type: Number, required: true }, // in ms
})
let duration = ref(0)

const isGettingEndTime = computed(() => props.totalDuration === 0)
const latency = computed(() => Math.abs(duration.value - props.executionTime).toFixed(4))

watch(
  () => props.executionTime,
  (v) => {
    if (v === -1) updateSecond()
  },
  { immediate: true }
)

watch(
  () => props.totalDuration,
  (v) => (duration.value = v),
  { immediate: true }
)

function updateSecond() {
  const now = new Date().valueOf()
  const currSec = ((now - props.startTime) / 1000).toFixed(4)
  if (isGettingEndTime.value) {
    duration.value = parseFloat(currSec)
    requestAnimationFrame(updateSecond)
  } else duration.value = props.totalDuration
}
</script>

<template>
  <div class="d-inline-flex flex-wrap">
    <div class="ml-4" data-test="exe-time">
      <span class="font-weight-bold">{{ $t('exeTime') }}:</span>
      {{ isGettingEndTime ? 'N/A' : `${executionTime} sec` }}
    </div>
    <div class="ml-4" data-test="latency-time">
      <span class="font-weight-bold">{{ $t('latency') }}:</span>
      {{ isGettingEndTime ? 'N/A' : `${latency} sec` }}
    </div>
    <div class="ml-4" data-test="total-time">
      <span class="font-weight-bold"> {{ $t('total') }}:</span>
      {{ isGettingEndTime ? Math.round(duration) : duration }} sec
    </div>
  </div>
</template>
