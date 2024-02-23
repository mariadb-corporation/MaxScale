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
import { Chart } from 'chart.js'
import { Line as LineChart } from 'vue-chartjs'
import ChartStreaming from '@robloche/chartjs-plugin-streaming'
import { mergeBaseOpts } from '@/components/common/Charts/utils'
import { streamTooltip } from '@/components/common/Charts/customTooltips'

Chart.register(ChartStreaming)
const props = defineProps({
  opts: { type: Object, default: () => {} },
  refreshRate: { type: Number, default: -1 },
})

const {
  lodash: { uniqueId, merge },
} = useHelpers()
const typy = useTypy()
const uniqueTooltipId = uniqueId('tooltip_')

let streamingOpt = ref({ delay: 0, frameRate: 24, pause: false })
const wrapper = ref(null)

const isPaused = computed(() => props.refreshRate === -1)

const options = computed(() =>
  merge(
    {
      elements: { point: { radius: 0 } },
      scales: {
        x: { type: 'realtime', ticks: { source: 'data' } },
        y: {
          beginAtZero: true,
          grid: {
            // Hide zero line and maximum line
            color: (context) =>
              context.tick.value === 0 || context.tick.value === context.chart.scales.y.max
                ? 'transparent'
                : 'rgba(234, 234, 234, 1)',
          },
          ticks: { maxTicksLimit: 3 },
        },
      },
      plugins: {
        streaming: streamingOpt.value,
        tooltip: {
          external: (context) => streamTooltip({ context, tooltipId: uniqueTooltipId }),
        },
      },
    },
    mergeBaseOpts(props.opts)
  )
)
onBeforeUnmount(() => {
  let tooltipEl = document.getElementById(uniqueTooltipId)
  if (tooltipEl) tooltipEl.remove()
})

/**
 * Handle pause/resume stream in the next tick for the initial render,
 * because chartInstance is only available in the nextTick
 */
watch(
  () => props.refreshRate,
  (v, oV) => {
    if (typy(oV).isUndefined) nextTick(() => handleStream())
    else handleStream()
  },
  {
    immediate: true,
  }
)

function handleStream() {
  streamingOpt.value.pause = isPaused.value
  streamingOpt.value.delay = isPaused.value
    ? streamingOpt.value.delay
    : (props.refreshRate + 2) * 1000
}

defineExpose({ wrapper })
</script>

<template>
  <LineChart ref="wrapper" :options="options" />
</template>
