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
import OverviewBlocks from '@/components/monitor/OverviewBlocks.vue'
import MonitorPageHeader from '@/components/details/MonitorPageHeader.vue'
import RelationshipTable from '@/components/details/RelationshipTable.vue'
import { MXS_OBJ_TYPES } from '@/constants'

const store = useStore()
const route = useRoute()
const typy = useTypy()
const {
  lodash: { isEmpty },
} = useHelpers()
const { items: serverItems, fetch: fetchServersAttrs } = useObjRelationshipData()
const { fetchObj, patchParams, patchRelationship } = useMxsObjActions(MXS_OBJ_TYPES.MONITORS)
const {
  fetch: fetchCsStatus,
  isLoading: isLoadingCsStatus,
  csStatus,
  noDataTxt: csStatusNoDataTxt,
} = useFetchCsStatus()

let isFirstFetch = ref(true)
let isCallingOp = ref(false)

const obj_data = computed(() => store.state.monitors.obj_data)
const should_refresh_resource = computed(() => store.state.should_refresh_resource)
const module_parameters = computed(() => store.state.module_parameters)
const all_servers = computed(() => store.state.servers.all_servers)
const isAdmin = computed(() => store.getters['users/isAdmin'])

const id = computed(() => obj_data.value.id)
const module = computed(() => typy(obj_data.value, 'attributes.module').safeString)
const state = computed(() => typy(obj_data.value, 'attributes.state').safeString)
const isColumnStoreCluster = computed(
  () => typy(obj_data.value, 'attributes.parameters.cs_admin_api_key').safeString
)
const monitoredServersData = computed(
  () => typy(obj_data.value, 'relationships.servers.data').safeArray
)
const unmonitoredServers = computed(() =>
  all_servers.value.reduce((acc, server) => {
    if (isEmpty(server.relationships.monitors)) {
      acc.push({ id: server.id, state: server.attributes.state, type: server.type })
    }
    return acc
  }, [])
)

watch(should_refresh_resource, async (v) => {
  if (v) {
    store.commit('SET_SHOULD_REFRESH_RESOURCE', false)
    await fetchAll()
  }
})
// re-fetch when the route changes
watch(
  () => route.path,
  async () => await fetchAll()
)
onBeforeMount(async () => {
  await initialFetch()
})

async function initialFetch() {
  await fetch()
  await store.dispatch('fetchModuleParameters', module.value)
  await fetchServersAttrs(monitoredServersData.value)
  await handleFetchCsStatus()
}

async function fetchAll() {
  await fetch()
  await fetchServersAttrs(monitoredServersData.value)
  await handleFetchCsStatus()
}

async function handleFetchCsStatus() {
  if (!isCallingOp.value && isColumnStoreCluster.value) {
    await fetchCsStatus({
      id: id.value,
      module: module.value,
      state: state.value,
      successCb: () => (isFirstFetch.value = false),
      pollingInterval: 1000,
    })
  }
}

async function fetch() {
  await fetchObj(route.params.id)
}

async function handlePatchRelationship({ type, data }) {
  await patchRelationship({
    relationshipType: type,
    id: obj_data.value.id,
    data,
    callback: async () => {
      await fetch()
      await fetchServersAttrs(monitoredServersData.value)
    },
  })
}

async function updateParams(data) {
  await patchParams({ id: obj_data.value.id, data, callback: fetch })
}

async function fetchAllServers() {
  await store.dispatch('servers/fetchAll')
}
</script>

<template>
  <ViewWrapper>
    <MonitorPageHeader
      :item="obj_data"
      :successCb="fetchAll"
      :onCountDone="fetchAll"
      :csStatus="csStatus"
      :fetchCsStatus="fetchCsStatus"
      class="pb-5"
      @is-calling-op="isCallingOp = $event"
    >
      <template #page-title="{ pageId }">
        <RouterLink :to="`/visualization/clusters/${pageId}`" class="rsrc-link">
          {{ pageId }}
        </RouterLink>
      </template>
    </MonitorPageHeader>
    <VSheet v-if="!$helpers.lodash.isEmpty(obj_data)" class="pl-6">
      <OverviewBlocks
        :item="obj_data"
        :module="module"
        :onSwitchoverSuccess="fetchAll"
        class="pb-3"
      />
      <VRow>
        <VCol cols="6">
          <ParametersTable
            :data="obj_data.attributes.parameters"
            :paramsInfo="module_parameters"
            :confirmEdit="updateParams"
            :mxsObjType="MXS_OBJ_TYPES.MONITORS"
          />
        </VCol>
        <VCol cols="6">
          <VRow>
            <VCol cols="12">
              <RelationshipTable
                :type="MXS_OBJ_TYPES.SERVERS"
                addable
                removable
                :data="serverItems"
                :getRelationshipData="fetchAllServers"
                :customAddableItems="unmonitoredServers"
                @confirm-update="handlePatchRelationship"
              />
            </VCol>
            <VCol v-if="isColumnStoreCluster && isAdmin" cols="12">
              <CollapsibleReadOnlyTbl
                :title="`${$t('csStatus')}`"
                :data="csStatus"
                expandAll
                :noDataText="csStatusNoDataTxt"
                :loading="isFirstFetch && isLoadingCsStatus ? 'primary' : false"
              />
            </VCol>
          </VRow>
        </VCol>
      </VRow>
    </VSheet>
  </ViewWrapper>
</template>
