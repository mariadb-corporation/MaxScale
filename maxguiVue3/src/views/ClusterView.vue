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
import { MXS_OBJ_TYPES, MRDB_MON } from '@/constants'
import MonitorPageHeader from '@/components/details/MonitorPageHeader.vue'
import { genCluster } from '@/utils/visualization'

const store = useStore()
const route = useRoute()
const typy = useTypy()
const { fetch: fetchCsStatus, csStatus } = useFetchCsStatus()
const { fetchObj } = useMxsObjActions(MXS_OBJ_TYPES.MONITORS)

const obj_data = computed(() => store.state.monitors.obj_data)
const serverMap = computed(() => store.getters['servers/map'])
const isColumnStoreCluster = computed(
  () => typy(obj_data.value, 'attributes.parameters.cs_admin_api_key').safeString
)
const module = computed(() => typy(obj_data.value, 'attributes.module').safeString)
const state = computed(() => typy(obj_data.value, 'attributes.state').safeString)
const cluster = computed(() => {
  if (module.value === MRDB_MON)
    return genCluster({ monitor: obj_data.value, serverMap: serverMap.value })
  return {}
})

let isCallingOp = ref(false)

onBeforeMount(async () => await fetchAll())

async function fetchAll() {
  await fetchCluster()
  await handleFetchCsStatus()
}

async function handleFetchCsStatus() {
  if (!isCallingOp.value && isColumnStoreCluster.value) {
    await fetchCsStatus({
      id: obj_data.value.id,
      module: module.value,
      state: state.value,
      pollingInterval: 1000,
    })
  }
}

async function fetchCluster() {
  await Promise.all([store.dispatch('servers/fetchAll'), fetchObj(route.params.id)])
}
</script>
<template>
  <ViewWrapper :overflow="false" fluid class="fill-height">
    <MonitorPageHeader
      :item="obj_data"
      :successCb="fetchCluster"
      :onCountDone="fetchCluster"
      :csStatus="csStatus"
      :fetchCsStatus="fetchCsStatus"
      :showGlobalSearch="false"
      @is-calling-op="isCallingOp = $event"
    >
      <template #page-title="{ pageId }">
        <RouterLink to="`/dashboard/monitors/${pageId}`" class="rsrc-link">
          {{ pageId }}
        </RouterLink>
      </template>
    </MonitorPageHeader>
  </ViewWrapper>
</template>
