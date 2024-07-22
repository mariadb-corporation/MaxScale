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

const startTime = computed(() => props.start)
const endTime = computed(() => props.end)
const { isRunning, count, elapsedTime } = useElapsedTimer(startTime, endTime)

const latency = computed(() => Math.abs(elapsedTime.value - props.execTime).toFixed(4))
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
