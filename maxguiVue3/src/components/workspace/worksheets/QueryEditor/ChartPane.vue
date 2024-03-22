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
import { objectTooltip } from '@/components/common/Charts/customTooltips'

const props = defineProps({
  modelValue: { type: Object, required: true },
  containerHeight: { type: Number, default: 0 },
  chartTypes: { type: Object, required: true }, // SQL_CHART_TYPES object
  axisTypes: { type: Object, required: true }, // CHART_AXIS_TYPES object
})
const emit = defineEmits(['update:modelValue', 'close-chart'])
const {
  lodash: { uniqueId },
  dateFormat,
  exportToJpeg,
} = useHelpers()
const typy = useTypy()

let uniqueTooltipId = ref(uniqueId('tooltip_'))
let dataPoint = ref(null)
let chartToolHeight = ref(0)
let chartToolRef = ref(null)
let chartCtrRef = ref(null)
let chartRef = ref(null)

const chartOpt = computed({
  get: () => props.modelValue,
  set: (v) => emit('update:modelValue', v),
})
const tableData = computed(() => chartOpt.value.tableData)
const chartData = computed(() => chartOpt.value.chartData)
const labels = computed(() => chartData.value.labels)
const axesType = computed(() => chartOpt.value.axesType)
const axisKeys = computed(() => chartOpt.value.axisKeys)
const type = computed(() => chartOpt.value.type)
const hasTrendline = computed(() => chartOpt.value.hasTrendline)
const chartWidth = computed(() => {
  if (autoSkipTick(axesType.value.x)) return 'unset'
  return `${Math.min(labels.value.length * 15, 15000)}px`
})

const chartHeight = computed(() => {
  let height = props.containerHeight - (chartToolHeight.value + 12)
  if (!autoSkipTick(axesType.value.y)) {
    /** When there is too many data points,
     * first, get min value between "overflow" height (labels.length * 15)
     * and max height threshold 15000. However, when there is too little data points,
     * the "overflow" height is smaller than container height, container height
     * should be chosen to make chart fit to its container
     */
    height = Math.max(height, Math.min(labels.value.length * 15, 15000))
  }
  return `${height}px`
})
const chartStyle = computed(() => ({ height: chartHeight.value, minWidth: chartWidth.value }))
const chartOptions = computed(() => {
  let options = {
    layout: { padding: { left: 12, bottom: 12, right: 24, top: 24 } },
    animation: { active: { duration: 0 } },
    onHover: (e, el) => {
      e.native.target.style.cursor = el[0] ? 'pointer' : 'default'
    },
    scales: {
      x: {
        type: axesType.value.x,
        title: {
          display: true,
          text: axisKeys.value.x,
          font: { size: 14 },
          padding: { top: 16 },
          color: '#424f62',
        },
        beginAtZero: true,
        ticks: getAxisTicks({ axisId: 'x', axisType: axesType.value.x }),
      },
      y: {
        type: axesType.value.y,
        title: {
          display: true,
          text: axisKeys.value.y,
          font: { size: 14 },
          padding: { bottom: 16 },
          color: '#424f62',
        },
        beginAtZero: true,
        ticks: getAxisTicks({ axisId: 'y', axisType: axesType.value.y }),
      },
    },
    plugins: {
      tooltip: {
        callbacks: {
          label(context) {
            dataPoint.value = tableData.value[context.dataIndex]
          },
        },
        external: (context) =>
          objectTooltip({
            context,
            tooltipId: uniqueTooltipId.value,
            dataPoint: dataPoint.value,
            axisKeys: axisKeys.value,
            alignTooltipToLeft: context.tooltip.caretX >= chartCtrRef.value.clientWidth / 2,
          }),
      },
    },
  }
  if (chartOpt.value.isHorizChart) options.indexAxis = 'y'
  return options
})

watch(chartData, (v) => {
  if (!typy(v, 'datasets[0].data[0]').safeObject) removeTooltip()
})

watch(hasTrendline, (v) => {
  let dataset = typy(getChartInstance(), 'data.datasets[0]').safeObjectOrEmpty
  if (v)
    dataset.trendlineLinear = {
      colorMin: '#2d9cdb',
      colorMax: '#2d9cdb',
      lineStyle: 'solid',
      width: 2,
    }
  else delete dataset.trendlineLinear
})
onMounted(() =>
  nextTick(() => {
    chartToolHeight.value = chartToolRef.value.offsetHeight
  })
)
onBeforeUnmount(() => removeTooltip())

function getChartInstance() {
  return typy(chartRef.value, 'wrapper.chart').safeObjectOrEmpty
}

/**
 * Check if provided axisType is either LINEAR OR TIME type.
 * @param {String} param.axisType - CHART_AXIS_TYPES
 * @returns {Boolean} - should autoSkip the tick
 */
function autoSkipTick(axisType) {
  const { LINEAR, TIME } = props.axisTypes
  return axisType === LINEAR || axisType === TIME
}

/**
 * Get the ticks object
 * @param {String} param.axisType - CHART_AXIS_TYPES
 * @param {String} param.axisId- x or y
 * @returns {Object} - ticks object
 */
function getAxisTicks({ axisId, axisType }) {
  const { CATEGORY } = props.axisTypes
  const autoSkip = autoSkipTick(axesType.value[axisType])
  let ticks = {
    autoSkip,
    callback: function (value) {
      // https://www.chartjs.org/docs/latest/axes/labelling.html#creating-custom-tick-formats
      const v = this.getLabelForValue(value)
      if (typy(v).isString && v.length > 10) return `${v.substr(0, 10)}...`
      return v
    },
  }
  if (autoSkip) ticks.autoSkipPadding = 15
  // only rotate tick label for the X axis and CATEGORY axis type
  if (axisId === 'x' && axisType === CATEGORY) {
    ticks.maxRotation = 90
    ticks.minRotation = 90
  }
  return ticks
}

function removeTooltip() {
  let tooltipEl = document.getElementById(uniqueTooltipId.value)
  if (tooltipEl) tooltipEl.remove()
}

function getDefFileName() {
  return `MaxScale ${type.value} Chart - ${dateFormat({
    value: new Date(),
  })}`
}

function exportChart() {
  exportToJpeg({ canvas: chartRef.value.$el, fileName: getDefFileName() })
}
</script>

<template>
  <div class="chart-pane d-flex flex-column fill-height">
    <div ref="chartToolRef" class="d-flex pt-2 pr-3">
      <VSpacer />
      <TooltipBtn icon variant="text" color="primary" density="compact" @click="exportChart">
        <template #btn-content><VIcon size="16" icon="$mdiDownload" /> </template>
        {{ $t('exportChart') }}
      </TooltipBtn>
      <TooltipBtn
        icon
        variant="text"
        color="primary"
        density="compact"
        @click="chartOpt.isMaximized = !chartOpt.isMaximized"
      >
        <template #btn-content
          ><VIcon size="20" :icon="`$mdiFullscreen${chartOpt.isMaximized ? 'Exit' : ''}`" />
        </template>
        {{ chartOpt.isMaximized ? $t('minimize') : $t('maximize') }}
      </TooltipBtn>
      <TooltipBtn
        class="close-chart"
        icon
        variant="text"
        color="primary"
        density="compact"
        @click="$emit('close-chart')"
      >
        <template #btn-content> <VIcon size="11" icon="mxs:close" /> </template>
        {{ $t('close') }}
      </TooltipBtn>
    </div>
    <div ref="chartCtrRef" class="w-100 overflow-auto fill-height">
      <div class="canvas-container" :style="chartStyle">
        <LineChart
          v-if="type === chartTypes.LINE"
          ref="chartRef"
          hasVertCrossHair
          :data="chartData"
          :opts="chartOptions"
        />
        <ScatterChart
          v-else-if="type === chartTypes.SCATTER"
          ref="chartRef"
          :data="chartData"
          :opts="chartOptions"
        />
        <BarChart
          v-else-if="type === chartTypes.BAR_VERT || type === chartTypes.BAR_HORIZ"
          ref="chartRef"
          :data="chartData"
          :opts="chartOptions"
        />
      </div>
    </div>
  </div>
</template>
