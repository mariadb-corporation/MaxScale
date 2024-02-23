<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import PageHeader from '@/components/dashboard/PageHeader.vue'
import DashboardGraphs from '@/components/dashboard/DashboardGraphs.vue'

const store = useStore()
const typy = useTypy()

let activeTab = ref(null)
const graphsRef = ref(null)
const TABS = ['servers', 'sessions', 'services', 'listeners', 'filters']
const tabActions = TABS.map((name) => () => store.dispatch(`${name}/fetchAll`))

const countMap = computed(() => {
  return {
    filters: store.getters['filters/total'],
    listeners: store.getters['listeners/total'],
    servers: store.getters['servers/total'],
    services: store.getters['services/total'],
    sessions: store.getters['sessions/total'],
  }
})

onBeforeMount(async () => {
  await store.dispatch('maxscale/fetchMaxScaleOverviewInfo')
  await fetchAll()
  // Init graph datasets
  await typy(graphsRef.value, 'initDatasets').safeFunction()
})

async function fetchAll() {
  await Promise.all([
    store.dispatch('maxscale/fetchThreadStats'),
    store.dispatch('monitors/fetchAll'),
    ...tabActions.map((action) => action()),
    store.dispatch('maxscale/fetchConfigSync'),
  ])
}

async function onCountDone() {
  const timestamp = Date.now()
  await Promise.all([fetchAll(), typy(graphsRef.value, 'updateChart').safeFunction(timestamp)])
}

function loadTabComponent(name) {
  switch (name) {
    case 'servers':
      return defineAsyncComponent(() => import('@/components/dashboard/ServersTab.vue'))
    default:
      return 'div'
  }
}

defineExpose({ TABS })
</script>
<template>
  <ViewWrapper>
    <VSheet>
      <PageHeader :onCountDone="onCountDone" />
      <DashboardGraphs ref="graphsRef" />
      <VTabs v-model="activeTab">
        <VTab v-for="name in TABS" :key="name" :to="`/dashboard/${name}`">
          {{ $t(name === 'sessions' ? 'currentSessions' : name, 2) }}
          <span class="grayed-out-info"> ({{ countMap[name] }}) </span>
        </VTab>
      </VTabs>
      <VWindow v-model="activeTab" class="fill-height">
        <VWindowItem v-for="name in TABS" :key="name" class="pt-2">
          <component :is="loadTabComponent(name)" />
        </VWindowItem>
      </VWindow>
    </VSheet>
  </ViewWrapper>
</template>
