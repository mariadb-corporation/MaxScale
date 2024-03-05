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

const props = defineProps({ item: { type: Object, required: true } })

const store = useStore()
const typy = useTypy()
const { t } = useI18n()
const {
  genLineStreamDataset,
  dateFormat,
  lodash: { cloneDeep },
} = useHelpers()

const connections = computed(() => typy(props.item, 'attributes.statistics.connections').safeNumber)
const totalConnections = computed(
  () => typy(props.item, 'attributes.statistics.total_connections').safeNumber
)

const currRefreshRate = computed(() => store.getters['currRefreshRate'])

const overviewInfo = computed(() => {
  const { attributes: { router, started } = {} } = props.item
  return { router, started: dateFormat({ value: started }) }
})

let connectionsDatasets = ref([])
const graphRef = ref(null)

const graphData = computed(() => ({
  title: t('currentConnections', 2),
  datasets: cloneDeep(connectionsDatasets.value),
}))

onMounted(() => genConnectionDatasets())

function genConnectionDatasets() {
  connectionsDatasets.value = [
    genLineStreamDataset({ label: graphData.value.title, value: connections.value, colorIndex: 0 }),
  ]
}

function updateChart() {
  const graph = typy(graphRef.value, 'wrapper.chart').safeObjectOrEmpty
  if (graph)
    graph.data.datasets.forEach((dataset) => {
      dataset.data.push({ x: Date.now(), y: connections.value })
    })
}
defineExpose({ updateChart })
</script>

<template>
  <VSheet class="d-flex mb-2">
    <div class="d-flex" style="width: 40%">
      <OutlinedOverviewCard
        v-for="(value, name, index) in overviewInfo"
        :key="name"
        wrapperClass="mt-5"
        class="px-10 rounded-0"
      >
        <template #title>
          <span :style="{ visibility: index === 0 ? 'visible' : 'hidden' }">
            {{ $t('overview') }}
          </span>
        </template>
        <template #card-body>
          <span class="text-caption text-uppercase font-weight-bold text-deep-ocean">
            {{ name }}
          </span>
          <span class="text-no-wrap text-body-2">
            {{ value }}
          </span>
        </template>
      </OutlinedOverviewCard>
    </div>
    <div style="width: 60%" class="pl-3">
      <OutlinedOverviewCard wrapperClass="mt-5">
        <template #title>
          {{ graphData.title }}
          <span class="text-lowercase font-weight-medium">
            ({{ connections }}/{{ totalConnections }})</span
          >
        </template>
        <template #card-body>
          <StreamLineChart
            ref="graphRef"
            :data="{ datasets: graphData.datasets }"
            :style="{ height: '70px' }"
            class="pl-1"
            :refreshRate="currRefreshRate"
          />
        </template>
      </OutlinedOverviewCard>
    </div>
  </VSheet>
</template>
