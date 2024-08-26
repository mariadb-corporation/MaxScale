<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { CHART_TYPE_MAP, CHART_AXIS_TYPE_MAP } from '@/constants/workspace'

const props = defineProps({
  modelValue: { type: Object, required: true },
  resultSets: { type: Array, required: true },
})
const emit = defineEmits(['update:modelValue', 'is-chart-ready'])

const { LINE, SCATTER, BAR_VERT, BAR_HORIZ } = CHART_TYPE_MAP
const { CATEGORY, LINEAR, TIME } = CHART_AXIS_TYPE_MAP
const typy = useTypy()
const {
  lodash: { isEqual },
  dynamicColors,
  strReplaceAt,
  map2dArr,
} = useHelpers()

// inputs
const resSet = ref(null)
const axisKeyMap = ref({ x: '', y: '' })
const axisTypeMap = ref({ x: '', y: '' })
const showTrendline = ref(false)

const chartConfig = computed({
  get: () => props.modelValue,
  set: (v) => emit('update:modelValue', v),
})

const axisFields = computed(() => typy(resSet.value, 'fields').safeArray)
const isHorizChart = computed(() => chartConfig.value.type === BAR_HORIZ)
const supportTrendLine = computed(() =>
  [LINE, SCATTER, BAR_VERT, BAR_HORIZ].includes(chartConfig.value.type)
)
const hasLinearAxis = computed(
  () => axisTypeMap.value.x === LINEAR || axisTypeMap.value.y === LINEAR
)
const isChartReady = computed(() =>
  Boolean(
    typy(chartConfig.value, 'type').safeString &&
      typy(chartConfig.value, 'chartData.labels').safeArray.length
  )
)

watch(
  () => props.resultSets,
  (v, oV) => {
    if (!isEqual(v, oV)) {
      clearAxes()
      resSet.value = null
      genChartData()
    }
  },
  { deep: true }
)
watch(
  () => chartConfig.value.type,
  () => clearAxes()
)
watch(resSet, () => clearAxes(), { deep: true })
watch(showTrendline, () => genChartData())
watch(isChartReady, (v) => emit('is-chart-ready', v))
watch(axisKeyMap, () => genChartData(), { deep: true })
watch(axisTypeMap, () => genChartData(), { deep: true })

function resetChartConfig() {
  chartConfig.value = {
    type: '',
    chartData: { datasets: [], labels: [] },
    axisKeyMap: { x: '', y: '' },
    axisTypeMap: { x: '', y: '' },
    tableData: [],
    isHorizChart: false,
    hasTrendline: false,
  }
}

function clearAxes() {
  axisKeyMap.value = { x: '', y: '' }
  axisTypeMap.value = { x: '', y: '' }
}

function labelingAxisType(axisType) {
  switch (axisType) {
    case LINEAR:
      return `${axisType} (Numerical data)`
    case CATEGORY:
      return `${axisType} (String data)`
    default:
      return axisType
  }
}

function genDatasetProperties() {
  const lineColor = dynamicColors(0)

  const indexOfOpacity = lineColor.lastIndexOf(')') - 1
  const backgroundColor = strReplaceAt({
    str: lineColor,
    index: indexOfOpacity,
    newChar: '0.2',
  })
  switch (chartConfig.value.type) {
    case LINE:
      return {
        fill: true,
        backgroundColor: backgroundColor,
        borderColor: lineColor,
        borderWidth: 1,
        pointBorderColor: 'transparent',
        pointBackgroundColor: 'transparent',
        pointHoverBorderColor: lineColor,
        pointHoverBackgroundColor: backgroundColor,
      }
    case SCATTER:
      return {
        borderWidth: 1,
        fill: true,
        backgroundColor: backgroundColor,
        borderColor: lineColor,
        pointHoverBackgroundColor: lineColor,
        pointHoverRadius: 5,
      }
    case BAR_VERT:
    case BAR_HORIZ:
      return {
        barPercentage: 0.5,
        categoryPercentage: 1,
        barThickness: 'flex',
        maxBarThickness: 48,
        minBarLength: 2,
        backgroundColor: backgroundColor,
        borderColor: lineColor,
        borderWidth: 1,
        hoverBackgroundColor: lineColor,
        hoverBorderColor: '#4f5051',
      }
    default:
      return {}
  }
}

/** This mutates sorting chart data for LINEAR or TIME axis
 * @param {Array} tableData - Table data
 */
function sortLinearOrTimeData(tableData) {
  let axisId = 'y'
  // For vertical graphs, sort only the y axis, but for horizontal, sort the x axis
  if (isHorizChart.value) axisId = 'x'
  const axisType = axisTypeMap.value[axisId]
  if (axisType === LINEAR || axisType === TIME) {
    tableData.sort((a, b) => {
      const valueA = a[axisKeyMap.value[axisId]]
      const valueB = b[axisKeyMap.value[axisId]]
      if (axisType === LINEAR) return valueA - valueB
      return new Date(valueA) - new Date(valueB)
    })
  }
}

function genChartData() {
  const { x, y } = axisKeyMap.value
  const { x: xType, y: yType } = axisTypeMap.value

  const chartData = {
    datasets: [{ data: [], ...genDatasetProperties() }],
    labels: [],
  }
  const tableData = map2dArr({
    fields: typy(resSet.value, 'fields').safeArray,
    arr: typy(resSet.value, 'data').safeArray,
  })
  if (x && y && xType && yType) {
    sortLinearOrTimeData(tableData)
    tableData.forEach((row) => {
      const dataPoint = isHorizChart.value ? row[x] : row[y]
      const label = isHorizChart.value ? row[y] : row[x]
      chartData.datasets[0].data.push(dataPoint)
      chartData.labels.push(label)
    })
  }
  chartConfig.value = {
    ...chartConfig.value,
    chartData,
    axisKeyMap: { x, y },
    axisTypeMap: { x: xType, y: yType },
    tableData,
    isHorizChart: isHorizChart.value,
    hasTrendline: hasLinearAxis.value && showTrendline.value && supportTrendLine.value,
  }
}

defineExpose({ resetChartConfig })
</script>

<template>
  <div class="pa-4">
    <h5 class="text-h5 mb-4">{{ $t('visualization') }}</h5>
    <label class="label-field text-small-text label--required" for="chart-type-select">
      {{ $t('graph') }}
    </label>
    <VSelect
      v-model="chartConfig.type"
      :items="Object.values(CHART_TYPE_MAP)"
      hide-details="auto"
      id="chart-type-select"
    />
    <div v-if="chartConfig.type" class="mt-4">
      <label class="label-field text-small-text label--required" for="result-set-select">
        {{ $t('selectResultSet') }}
      </label>
      <VSelect
        v-model="resSet"
        :items="resultSets"
        item-title="id"
        item-value="id"
        return-object
        hide-details="auto"
        id="result-set-select"
      />
      <template v-if="resSet">
        <!-- Don't show axisKeyMap inputs if result set is empty -->
        <div
          v-if="$typy(resSet, 'data').isEmptyArray"
          class="mt-4 text-small-text"
          data-test="empty-set"
        >
          {{ $t('emptySet') }}
        </div>
        <template v-else>
          <div v-for="(_, axisId) in axisKeyMap" :key="axisId">
            <div class="mt-2">
              <label
                class="label-field text-small-text text-capitalize label--required"
                :for="`${axisId}-axis-select`"
              >
                {{ axisId }} axis
              </label>
              <VSelect
                v-model="axisKeyMap[axisId]"
                :items="axisFields"
                hide-details="auto"
                :id="`${axisId}-axis-select`"
              />
            </div>
            <div class="mt-2">
              <label
                class="label-field text-small-text text-capitalize label--required"
                :for="`${axisId}-axis-type-select`"
              >
                {{ axisId }} axis type
              </label>
              <VSelect
                v-model="axisTypeMap[axisId]"
                :items="Object.values(CHART_AXIS_TYPE_MAP)"
                hide-details="auto"
                :id="`${axisId}-axis-type-select`"
              >
                <template #selection="{ item }"> {{ labelingAxisType(item.title) }} </template>
                <template #item="{ props }">
                  <VListItem v-bind="props">
                    <template #title="{ title }"> {{ labelingAxisType(title) }} </template>
                  </VListItem>
                </template>
              </VSelect>
            </div>
          </div>
        </template>
        <VCheckboxBtn
          v-if="supportTrendLine"
          v-model="showTrendline"
          density="compact"
          class="mt-3 ml-n1"
          data-test="show-trend-line"
        >
          <template #label>
            {{ $t('showTrendline') }}
            <VTooltip location="top">
              <template #activator="{ props }">
                <VIcon
                  class="ml-1 material-icons-outlined cursor--pointer"
                  size="16"
                  color="info"
                  icon="$mdiInformationOutline"
                  v-bind="props"
                />
              </template>
              {{ $t('info.showTrendline') }}
            </VTooltip>
          </template>
        </VCheckboxBtn>
      </template>
    </div>
  </div>
</template>
