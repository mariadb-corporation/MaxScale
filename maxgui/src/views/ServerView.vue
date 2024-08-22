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
import { MXS_OBJ_TYPE_MAP, SERVER_OP_TYPE_MAP } from '@/constants'
import ViewHeader from '@/components/details/ViewHeader.vue'
import TabOne from '@/components/server/TabOne.vue'
import TabTwo from '@/components/server/TabTwo.vue'
import OverviewBlocks from '@/components/server/OverviewBlocks.vue'
import sessionsService from '@/services/sessionsService'
import monitorsService from '@/services/monitorsService'
import { getFrameIdx } from '@/utils/statusIconHelpers'
import { useOpMap } from '@/composables/servers'

const store = useStore()
const route = useRoute()
const { t } = useI18n()
const typy = useTypy()

const { items: serviceItems, fetch: fetchServicesAttrs } = useObjRelationshipData()
const { fetchObj, patchParams, patchRelationship } = useMxsObjActions(MXS_OBJ_TYPE_MAP.SERVERS)
const fetchModuleParams = useFetchModuleParams()

const TABS = [
  { name: `${t('statistics', 2)} & ${t('sessions', 2)}` },
  { name: `${t('parameters', 2)} & ${t('diagnostics', 2)}` },
]

const activeTabIdx = ref(0)
const forceClosing = ref(false)

const obj_data = computed(() => store.state.servers.obj_data)
const should_refresh_resource = computed(() => store.state.should_refresh_resource)

const servicesData = computed(() => typy(obj_data.value, 'relationships.services.data').safeArray)
const objState = computed(() => typy(obj_data.value, 'attributes.state').safeString)
const serverHealthy = computed(() => {
  switch (getFrameIdx(MXS_OBJ_TYPE_MAP.SERVERS, objState.value)) {
    case 0:
      return t('unHealthy')
    case 1:
      return t('healthy')
    default:
      return t('maintenance')
  }
})
const versionString = computed(() => typy(obj_data.value, 'attributes.version_string').safeString)

const { computedMap: computedServerOpMap, handler: opHandler } = useOpMap(objState)

const operationMatrix = computed(() => {
  const { MAINTAIN, CLEAR, DRAIN, DELETE } = SERVER_OP_TYPE_MAP
  const serverOpMap = computedServerOpMap.value
  return [[serverOpMap[MAINTAIN], serverOpMap[CLEAR]], [serverOpMap[DRAIN]], [serverOpMap[DELETE]]]
})

const filterSessionParam = computed(
  () => `filter=/attributes/connections/0/server="${route.params.id}"`
)

// re-fetch when the route changes
watch(
  () => route.path,
  async () => await fetchAll()
)
watch(activeTabIdx, async () => {
  await fetchByActiveTab()
})
watch(should_refresh_resource, async (v) => {
  if (v) {
    store.commit('SET_SHOULD_REFRESH_RESOURCE', false)
    await fetchAll()
  }
})

onBeforeMount(async () => {
  await fetchAll()
  await fetchModuleParams(MXS_OBJ_TYPE_MAP.SERVERS)
})

async function fetchAll() {
  await fetch()
  await fetchByActiveTab()
}

async function fetchByActiveTab() {
  activeTabIdx.value === 0 ? await fetchTabOneData() : await fetchTabTwoData()
}

async function fetchTabOneData() {
  await Promise.all([
    fetchServicesAttrs(servicesData.value),
    sessionsService.fetchSessions(filterSessionParam.value),
  ])
}

async function fetchTabTwoData() {
  await fetchMonitorDiagnostics()
}

async function fetchMonitorDiagnostics() {
  const { relationships: { monitors = {} } = {} } = obj_data.value
  if (monitors.data) await monitorsService.fetchDiagnostics(typy(monitors, 'data[0].id').safeString)
  else store.commit('monitors/SET_MONITOR_DIAGNOSTICS', {})
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
      if (type === MXS_OBJ_TYPE_MAP.SERVICES) await fetchServicesAttrs(servicesData.value)
      else await fetchMonitorDiagnostics()
    },
  })
}

async function onConfirmOp({ op, id }) {
  await opHandler({ op, id, forceClosing: forceClosing.value, successCb: fetch })
}

const activeTab = computed(() =>
  activeTabIdx.value === 0
    ? {
        component: TabOne,
        props: {
          obj_data: obj_data.value,
          serviceItems: serviceItems.value,
          handlePatchRelationship,
          fetchSessions: async () => await sessionsService.fetchSessions(filterSessionParam.value),
        },
      }
    : { component: TabTwo, props: { obj_data: obj_data.value, patchParams, fetch } }
)
</script>

<template>
  <ViewWrapper>
    <ViewHeader
      :item="obj_data"
      :type="MXS_OBJ_TYPE_MAP.SERVERS"
      showStateIcon
      :stateLabel="serverHealthy"
      :operationMatrix="operationMatrix"
      :onConfirm="onConfirmOp"
      :onCountDone="fetchAll"
    >
      <template #confirm-dlg-body-append="{ confirmDlg }">
        <VCheckboxBtn
          v-if="confirmDlg.type === SERVER_OP_TYPE_MAP.MAINTAIN"
          v-model="forceClosing"
          class="mt-4 ml-n2"
          :label="$t('forceClosing')"
        />
      </template>
      <template #state-append>
        <span v-if="versionString" class="text-grayed-out text-body-2">
          |
          {{ $t('version') }} {{ versionString }}
        </span>
      </template>
    </ViewHeader>
    <VSheet v-if="!$helpers.lodash.isEmpty(obj_data)" class="pl-6">
      <OverviewBlocks :item="obj_data" :handlePatchRelationship="handlePatchRelationship" />
      <VTabs v-model="activeTabIdx">
        <VTab v-for="tab in TABS" :key="tab.name" class="text-primary"> {{ tab.name }} </VTab>
      </VTabs>
      <VWindow v-model="activeTabIdx">
        <VWindowItem v-for="(name, i) in TABS" :key="name" class="pt-5">
          <component v-if="activeTabIdx === i" :is="activeTab.component" v-bind="activeTab.props" />
        </VWindowItem>
      </VWindow>
    </VSheet>
  </ViewWrapper>
</template>
