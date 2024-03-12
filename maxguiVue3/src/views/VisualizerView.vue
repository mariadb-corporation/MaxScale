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
import ConfigTab from '@/components/visualizer/ConfigTab.vue'
import ClustersTab from '@/components/visualizer/ClustersTab.vue'

const route = useRoute()
const store = useStore()

const TABS_MAP = { CONFIG: 'configuration', CLUSTERS: 'clusters' }

const TABS = Object.values(TABS_MAP)
let activeTab = ref(route.params.id)

onMounted(async () => await fetchByActiveTab())

watch(activeTab, async () => await fetchByActiveTab())

async function fetchByActiveTab() {
  activeTab.value === TABS_MAP.CONFIG ? await fetchTabOneData() : await fetchTabTwoData()
}

async function fetchTabOneData() {
  await Promise.all([
    store.dispatch('monitors/fetchAll'),
    store.dispatch('servers/fetchAll'),
    store.dispatch('services/fetchAll'),
    store.dispatch('filters/fetchAll'),
    store.dispatch('listeners/fetchAll'),
  ])
}

async function fetchTabTwoData() {
  await Promise.all([store.dispatch('servers/fetchAll'), store.dispatch('monitors/fetchAll')])
}

async function onCountDone() {
  await fetchByActiveTab()
}

function loadTabComponent(name) {
  switch (name) {
    case TABS_MAP.CONFIG:
      return ConfigTab
    default:
      return ClustersTab
  }
}
</script>

<template>
  <ViewWrapper
    :overflow="false"
    fluid
    class="fill-height"
    :spacerStyle="{ height: '100%', borderBottom: 'thin solid #e7eef1' }"
  >
    <portal to="view-header__left">
      <VTabs v-model="activeTab" class="flex-grow-0">
        <VTab v-for="name in TABS" :key="name" :to="`/visualization/${name}`" :value="name">
          {{ name }}
        </VTab>
      </VTabs>
    </portal>
    <portal to="view-header__right">
      <div class="d-flex align-center fill-height" :style="{ borderBottom: 'thin solid #e7eef1' }">
        <RefreshRate :onCountDone="onCountDone" />
        <CreateMxsObj v-if="activeTab === TABS_MAP.CONFIG" class="ml-4 d-inline-block" />
      </div>
    </portal>
    <VWindow v-model="activeTab" class="fill-height">
      <VWindowItem v-for="name in TABS" :key="name" :value="name" class="pt-5 fill-height">
        <component :is="loadTabComponent(activeTab)" />
      </VWindowItem>
    </VWindow>
  </ViewWrapper>
</template>
