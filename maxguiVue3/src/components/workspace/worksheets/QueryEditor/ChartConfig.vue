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
const props = defineProps({
  modelValue: { type: Object, required: true },
  chartTypes: { type: Object, required: true }, // SQL_CHART_TYPES object
  axisTypes: { type: Object, required: true }, // CHART_AXIS_TYPES object
  queryModes: { type: Object, required: true }, // QUERY_MODES object
  resultSets: { type: Array, required: true },
})
const emit = defineEmits(['update:modelValue'])

const typy = useTypy()
const {
  lodash: { isEqual },
  dynamicColors,
  strReplaceAt,
  map2dArr,
} = useHelpers()

let resSet = ref(null)

let axisKeys = ref({ x: '', y: '' }) // axisKeys inputs
let axesType = ref({ x: '', y: '' }) // axesType inputs
let showTrendline = ref(false)

let chartOpt = computed({
  get: () => props.modelValue,
  set: (v) => emit('update:modelValue', v),
})

const axisFields = computed(() => {
  if (typy(resSet.value, 'fields').isEmptyArray) return []
  return resSet.value.fields
})
const isHorizChart = computed(() => chartOpt.value.type === props.chartTypes.BAR_HORIZ)
const supportTrendLine = computed(() => {
  const { LINE, SCATTER, BAR_VERT, BAR_HORIZ } = props.chartTypes
  return [LINE, SCATTER, BAR_VERT, BAR_HORIZ].includes(chartOpt.value.type)
})
const hasLinearAxis = computed(() => {
  const { LINEAR } = props.axisTypes
  return axesType.value.x === LINEAR || axesType.value.y === LINEAR
})

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
  () => chartOpt.value.type,
  () => clearAxes()
)
watch(resSet, () => clearAxes(), { deep: true })
watch(axisKeys, () => genChartData(), { deep: true })
watch(axesType, () => genChartData(), { deep: true })
watch(showTrendline, () => genChartData())

function clearAxes() {
  axisKeys.value = { x: '', y: '' }
  axesType.value = { x: '', y: '' }
}

function labelingAxisType(axisType) {
  const { LINEAR, CATEGORY } = props.axisTypes
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
  let dataset = {}
  const indexOfOpacity = lineColor.lastIndexOf(')') - 1
  const backgroundColor = strReplaceAt({
    str: lineColor,
    index: indexOfOpacity,
    newChar: '0.2',
  })
  const { LINE, SCATTER, BAR_VERT, BAR_HORIZ } = props.chartTypes
  switch (chartOpt.value.type) {
    case LINE:
      {
        dataset = {
          fill: true,
          backgroundColor: backgroundColor,
          borderColor: lineColor,
          borderWidth: 1,
          pointBorderColor: 'transparent',
          pointBackgroundColor: 'transparent',
          pointHoverBorderColor: lineColor,
          pointHoverBackgroundColor: backgroundColor,
        }
      }
      break
    case SCATTER: {
      dataset = {
        borderWidth: 1,
        fill: true,
        backgroundColor: backgroundColor,
        borderColor: lineColor,
        pointHoverBackgroundColor: lineColor,
        pointHoverRadius: 5,
      }
      break
    }
    case BAR_VERT:
    case BAR_HORIZ: {
      dataset = {
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
      break
    }
  }
  return dataset
}

/** This mutates sorting chart data for LINEAR or TIME axis
 * @param {Array} tableData - Table data
 */
function sortLinearOrTimeData(tableData) {
  const { BAR_HORIZ } = props.chartTypes
  let axisId = 'y'
  // For vertical graphs, sort only the y axis, but for horizontal, sort the x axis
  if (chartOpt.value.type === BAR_HORIZ) axisId = 'x'
  const axisType = axesType.value[axisId]
  const { LINEAR, TIME } = props.axisTypes
  if (axisType === LINEAR || axisType === TIME) {
    tableData.sort((a, b) => {
      const valueA = a[axisKeys.value[axisId]]
      const valueB = b[axisKeys.value[axisId]]
      if (axisType === props.axisTypes.LINEAR) return valueA - valueB
      return new Date(valueA) - new Date(valueB)
    })
  }
}

function genChartData() {
  const { x, y } = axisKeys.value
  let chartData = {
    datasets: [{ data: [], ...genDatasetProperties() }],
    labels: [],
  }
  const tableData = map2dArr({
    fields: typy(resSet.value, 'fields').safeArray,
    arr: typy(resSet.value, 'data').safeArray,
  })
  if (x && y && axesType.value.x && axesType.value.y) {
    sortLinearOrTimeData(tableData)
    tableData.forEach((row) => {
      const dataPoint = isHorizChart.value ? row[x] : row[y]
      const label = isHorizChart.value ? row[y] : row[x]
      chartData.datasets[0].data.push(dataPoint)
      chartData.labels.push(label)
    })
  }
  chartOpt.value = {
    ...chartOpt.value,
    chartData,
    axisKeys,
    axesType,
    tableData,
    isHorizChart,
    hasTrendline: hasLinearAxis.value && showTrendline.value && supportTrendLine.value,
  }
}
</script>

<template>
  <div class="pa-4">
    <h5 class="text-h5 mb-4">{{ $t('visualization') }}</h5>
    <label class="field__label text-small-text label-required" for="chart-type-select">
      {{ $t('graph') }}
    </label>
    <VSelect
      v-model="chartOpt.type"
      :items="Object.values(chartTypes)"
      hide-details="auto"
      id="chart-type-select"
    />
    <div v-if="chartOpt.type" class="mt-4">
      <label class="field__label text-small-text label-required" for="resultset-select">
        {{ $t('selectResultSet') }}
      </label>
      <VSelect
        v-model="resSet"
        :items="resultSets"
        item-title="id"
        item-value="id"
        return-object
        hide-details="auto"
        id="resultset-select"
      />
      <template v-if="resSet">
        <!-- Don't show axisKeys inputs if result set is empty -->
        <div v-if="$typy(resSet, 'data').isEmptyArray" class="mt-4 text-small-text">
          {{ $t('emptySet') }}
        </div>
        <template v-else>
          <div v-for="(_, axisId) in axisKeys" :key="axisId">
            <div class="mt-2">
              <label
                class="field__label text-small-text text-capitalize label-required"
                :for="`${axisId}-axis-select`"
              >
                {{ axisId }} axis
              </label>
              <VSelect
                v-model="axisKeys[axisId]"
                :items="axisFields"
                hide-details="auto"
                :id="`${axisId}-axis-select`"
              />
            </div>
            <div class="mt-2">
              <label
                class="field__label text-small-text text-capitalize label-required"
                :for="`${axisId}-axis-type-select`"
              >
                {{ axisId }} axis type
              </label>
              <VSelect
                v-model="axesType[axisId]"
                :items="Object.values(axisTypes)"
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
        <VCheckbox
          v-if="supportTrendLine"
          v-model="showTrendline"
          color="primary"
          class="mt-2"
          hide-details
        >
          <template #label>
            {{ $t('showTrendline') }}
            <VTooltip location="top" transition="slide-y-transition">
              <template #activator="{ props }">
                <VIcon
                  class="ml-1 material-icons-outlined pointer"
                  size="16"
                  color="info"
                  icon="$mdiInformationOutline"
                  v-bind="props"
                />
              </template>
              {{ $t('info.showTrendline') }}
            </VTooltip>
          </template>
        </VCheckbox>
      </template>
    </div>
  </div>
</template>
