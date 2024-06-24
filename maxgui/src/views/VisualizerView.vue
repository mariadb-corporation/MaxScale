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
import ConfigTab from '@/components/visualizer/ConfigTab.vue'
import ClustersTab from '@/components/visualizer/ClustersTab.vue'
import { MXS_OBJ_TYPES } from '@/constants'

const { SERVICES, SERVERS, MONITORS, LISTENERS, FILTERS } = MXS_OBJ_TYPES

const route = useRoute()
const fetchObjects = useFetchObjects()

const TABS_MAP = { CONFIG: 'configuration', CLUSTERS: 'clusters' }

const TABS = Object.values(TABS_MAP)
const activeTab = ref(route.params.id)
const isLoadingInitialData = ref(true)

onMounted(async () => {
  isLoadingInitialData.value = true
  await fetchByActiveTab()
  isLoadingInitialData.value = false
})

watch(activeTab, async () => await fetchByActiveTab())

async function fetchByActiveTab() {
  activeTab.value === TABS_MAP.CONFIG ? await fetchTabOneData() : await fetchTabTwoData()
}

async function fetchTabOneData() {
  await Promise.all([
    fetchObjects(MONITORS),
    fetchObjects(SERVERS),
    fetchObjects(SERVICES),
    fetchObjects(FILTERS),
    fetchObjects(LISTENERS),
  ])
}

async function fetchTabTwoData() {
  await Promise.all([fetchObjects(SERVERS), fetchObjects(MONITORS)])
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
        <VTab
          v-for="name in TABS"
          :key="name"
          :to="`/visualization/${name}`"
          :value="name"
          class="text-primary"
        >
          {{ name }}
        </VTab>
      </VTabs>
    </portal>
    <portal to="view-header__right">
      <div class="d-flex align-center fill-height border-bottom--table-border">
        <RefreshRate :onCountDone="onCountDone" />
        <portal-target name="view-header__right--append" />
        <CreateMxsObj v-if="activeTab === TABS_MAP.CONFIG" class="ml-2 d-inline-block" />
      </div>
    </portal>
    <VWindow v-model="activeTab" class="fill-height">
      <VWindowItem v-for="name in TABS" :key="name" :value="name" class="pt-5 fill-height">
        <component v-if="!isLoadingInitialData" :is="loadTabComponent(activeTab)" />
      </VWindowItem>
    </VWindow>
  </ViewWrapper>
</template>
