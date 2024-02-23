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
import GraphCnfDlg from '@/components/dashboard/GraphCnfDlg.vue'

const store = useStore()
const { t } = useI18n()
const { lodash, genLineStreamDataset } = useHelpers()
const typy = useTypy()

const GRAPH_NAMES = Object.freeze({
  SESSIONS: 'sessions',
  CONNECTIONS: 'connections',
  LOAD: 'load',
})
const total_sessions = computed(() => store.state.sessions.total_sessions)
const all_servers = computed(() => store.state.servers.all_servers)
const thread_stats = computed(() => store.state.maxscale.thread_stats)

const dsh_graphs_cnf = computed(() => store.state.persisted.dsh_graphs_cnf)
const are_dsh_graphs_expanded = computed(() => store.state.persisted.are_dsh_graphs_expanded)

const totalSessions = computed(() => store.getters['sessions/total'])
const currRefreshRate = computed(() => store.getters['currRefreshRate'])

const graphCardHeight = computed(() => (are_dsh_graphs_expanded.value ? 75 * 4 : 75))

const chartStyle = computed(() => ({ height: `${graphCardHeight.value}px` }))

const baseOpts = computed(() => ({
  layout: {
    padding: { top: 15, left: 4, bottom: -4 },
  },
  scales: {
    y: {
      ticks: { maxTicksLimit: are_dsh_graphs_expanded.value ? 6 : 3 },
    },
  },
}))

let isDlgOpened = ref(false)
let graphRef = ref({})
let loadDatasets = ref([])
let sessionsDatasets = ref([])
let serverConnsDatasets = ref([])

const graphs = computed(() => {
  return [
    {
      title: t('sessions', 2),
      name: GRAPH_NAMES.SESSIONS,
      datasets: lodash.cloneDeep(sessionsDatasets.value),
    },
    {
      title: t('connections', 2),
      name: GRAPH_NAMES.CONNECTIONS,
      datasets: lodash.cloneDeep(serverConnsDatasets.value),
    },
    {
      title: t('load'),
      name: GRAPH_NAMES.LOAD,
      datasets: lodash.cloneDeep(loadDatasets.value),
    },
  ]
})

const graphOptMap = computed(() => ({
  [GRAPH_NAMES.SESSIONS]: lodash.merge(
    { plugins: { annotation: { annotations: getAnnotation(GRAPH_NAMES.SESSIONS) } } },
    baseOpts.value
  ),
  [GRAPH_NAMES.CONNECTIONS]: lodash.merge(
    { plugins: { annotation: { annotations: getAnnotation(GRAPH_NAMES.CONNECTIONS) } } },
    baseOpts.value
  ),
  [GRAPH_NAMES.LOAD]: lodash.merge(
    {
      scales: { y: { max: 100, min: 0 } },
      plugins: { annotation: { annotations: getAnnotation(GRAPH_NAMES.LOAD) } },
    },
    baseOpts.value
  ),
}))

function toggleExpansion(v) {
  store.commit('persisted/SET_ARE_DSH_GRAPHS_EXPANDED', v)
}

function genSessionsDatasets() {
  const dataset = genLineStreamDataset({
    label: 'Total sessions',
    value: total_sessions.value,
    colorIndex: 0,
  })
  sessionsDatasets.value = [dataset]
}

function genConnsDataSets() {
  if (all_servers.value.length) {
    let dataSets = []
    all_servers.value.forEach((server, i) => {
      const { id, attributes: { statistics: { connections = null } = {} } = {} } = server
      if (connections !== null) {
        const dataset = genLineStreamDataset({
          label: `Server ID - ${id}`,
          value: connections,
          colorIndex: i,
          id,
        })
        dataSets.push(dataset)
      }
    })

    serverConnsDatasets.value = dataSets
  }
}

function genLoadDatasets() {
  if (thread_stats.value.length) {
    let dataSets = []
    thread_stats.value.forEach((thread, i) => {
      const { attributes: { stats: { load: { last_second = null } = {} } = {} } = {} } = thread
      if (last_second !== null) {
        const dataset = genLineStreamDataset({
          label: `THREAD ID - ${thread.id}`,
          value: last_second,
          colorIndex: i,
        })
        dataSets.push(dataset)
      }
    })
    loadDatasets.value = dataSets
  }
}

/**
 * @public
 */
async function initDatasets() {
  await Promise.all([genSessionsDatasets(), genConnsDataSets(), genLoadDatasets()])
}

function updateSessionsGraph(graph, timestamp) {
  graph.data.datasets.forEach((dataset) => {
    dataset.data.push({
      x: timestamp,
      y: totalSessions.value,
    })
  })
}

function updateConnsGraph(graph, timestamp) {
  all_servers.value.forEach((server, i) => {
    const dataset = graph.data.datasets.find((d) => d.resourceId === server.id)
    const value = typy(server, 'attributes.statistics.connections').safeNumber
    // update existing datasets
    if (dataset) dataset.data.push({ x: timestamp, y: value })
    else {
      /*
       * Copy previous data of a dataset, this ensures new
       * datasets can be added while streaming datasets.
       * The value of each item should be 0 because
       * at previous timestamp, new servers aren't created yet.
       */
      let dataOfADataSet = typy(dataset, 'data').safeArray
      dataOfADataSet.forEach((item) => (item.y = 0))
      graph.data.datasets.push({
        ...genLineStreamDataset({
          label: `Server ID - ${server.id}`,
          value,
          colorIndex: i,
          id: server.id,
        }),
        data: [...dataOfADataSet, { x: timestamp, y: value }],
      })
    }
  })
}

function updateLoadGraph(graph, timestamp) {
  const datasets = graph.data.datasets
  thread_stats.value.forEach((thread, i) => {
    const { attributes: { stats: { load: { last_second = null } = {} } = {} } = {} } = thread
    if (graph.data.datasets[i]) {
      datasets[i].data.push({
        x: timestamp,
        y: last_second,
      })
    } else {
      const newDataSet = genLineStreamDataset({
        label: `THREAD ID - ${thread.id}`,
        value: last_second,
        colorIndex: i,
        timestamp,
      })
      datasets.push(newDataSet)
    }
  })
}

/**
 * @public
 * @param {number} timestamp
 * @public
 */
async function updateChart(timestamp) {
  await Promise.all([
    updateSessionsGraph(getGraphInstance(GRAPH_NAMES.SESSIONS), timestamp),
    updateConnsGraph(getGraphInstance(GRAPH_NAMES.CONNECTIONS), timestamp),
    updateLoadGraph(getGraphInstance(GRAPH_NAMES.LOAD), timestamp),
  ])
}

function getAnnotation(graphName) {
  return typy(dsh_graphs_cnf.value, `${graphName}.annotations`).safeObjectOrEmpty
}

function getGraphInstance(name) {
  return typy(graphRef.value[name], 'wrapper.chart').safeObjectOrEmpty
}

// Function to be called by parent
defineExpose({ initDatasets, updateChart })
</script>

<template>
  <VRow class="mx-n2">
    <VCol v-for="(graph, i) in graphs" :key="i" cols="4" class="px-2">
      <OutlinedOverviewCard :height="graphCardHeight">
        <template #title>
          <div class="d-flex align-center">
            <span> {{ graph.title }} </span>
            <VSpacer />
            <template v-if="i === graphs.length - 1">
              <VBtn
                class="text-anchor text-capitalize"
                variant="text"
                size="x-small"
                data-test="toggle-expansion-btn"
                @click="toggleExpansion(!are_dsh_graphs_expanded)"
              >
                {{ are_dsh_graphs_expanded ? $t('collapse') : $t('expand') }}
              </VBtn>
              <TooltipBtn
                btnClass="setting-btn ml-1"
                density="comfortable"
                variant="text"
                icon
                size="x-small"
                color="primary"
                data-test="setting-btn"
                @click="isDlgOpened = true"
              >
                <template #btn-content>
                  <VIcon size="14" icon="mxs:settings" />
                </template>
                {{ $t('configuration') }}
              </TooltipBtn>
            </template>
          </div>
        </template>
        <template #card-body>
          <StreamLineChart
            v-if="graph.datasets.length"
            :style="chartStyle"
            :data="{ datasets: graph.datasets }"
            :refreshRate="currRefreshRate"
            :opts="graphOptMap[graph.name]"
            :ref="(el) => (graphRef[graph.name] = el)"
          />
        </template>
      </OutlinedOverviewCard>
    </VCol>
    <GraphCnfDlg v-model="isDlgOpened" />
  </VRow>
</template>
