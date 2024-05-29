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
import { MXS_OBJ_TYPES, ROUTING_TARGET_RELATIONSHIP_TYPES, SERVICE_OP_TYPES } from '@/constants'
import ViewHeader from '@/components/details/ViewHeader.vue'
import OverviewBlocks from '@/components/service/OverviewBlocks.vue'
import TabOne from '@/components/service/TabOne.vue'
import TabTwo from '@/components/service/TabTwo.vue'
import sessionsService from '@/services/sessionsService'
import { useOpMap } from '@/composables/services'

const store = useStore()
const route = useRoute()
const { t } = useI18n()
const typy = useTypy()
const {
  lodash: { pickBy },
} = useHelpers()

const activeTabIdx = ref(0)
const overviewBlocks = ref(null)

const TABS = [
  { name: `${t('parameters', 2)} & ${t('relationships', 2)}` },
  { name: `${t('sessions', 2)} & ${t('diagnostics', 2)}` },
]

const { fetchObj, patchParams, patchRelationship } = useMxsObjActions(MXS_OBJ_TYPES.SERVICES)
const { items: routingTargetItems, fetch: fetchRoutingTargetsAttrs } = useObjRelationshipData()
const { items: filterItems, fetch: fetchFiltersAttrs } = useObjRelationshipData()
const { items: listenerItems, fetch: fetchListenersAttrs } = useObjRelationshipData()
const fetchModuleParams = useFetchModuleParams()

const should_refresh_resource = computed(() => store.state.should_refresh_resource)
const obj_data = computed(() => store.state.services.obj_data)
const state = computed(() => typy(obj_data.value, 'attributes.state').safeString)
const filtersData = computed(() => typy(obj_data.value, 'relationships.filters.data').safeArray)
const listenersData = computed(() => typy(obj_data.value, 'relationships.listeners.data').safeArray)
const routingTargetsData = computed(() => {
  const routingTargetMap = pickBy(
    typy(obj_data.value, 'relationships').safeObjectOrEmpty,
    (v, key) => ROUTING_TARGET_RELATIONSHIP_TYPES.includes(key)
  )
  return Object.values(routingTargetMap).reduce((acc, item) => {
    acc.push(...item.data)
    return acc
  }, [])
})

const { computedMap: computedServiceOpMap, handler: opHandler } = useOpMap(state)

const operationMatrix = computed(() => {
  const { STOP, START, DESTROY } = SERVICE_OP_TYPES
  const serviceOpMap = computedServiceOpMap.value
  return [[serviceOpMap[STOP], serviceOpMap[START]], [serviceOpMap[DESTROY]]]
})

const filterSessionParam = computed(
  () => `filter=/relationships/services/data/0/id="${route.params.id}"`
)

const module = computed(() => typy(obj_data.value, 'attributes.router').safeString)

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
  await fetchModuleParams(module.value)
})

async function fetchAll() {
  await fetch()
  await fetchByActiveTab()
  typy(overviewBlocks.value, 'updateChart').safeFunction()
}

async function fetch() {
  await fetchObj(route.params.id)
}

async function fetchByActiveTab() {
  activeTabIdx.value === 0 ? await fetchTabOneData() : await fetchTabTwoData()
}

async function fetchTabOneData() {
  /* fetching also gtid_current_pos so they can be included in the STATISTICS table in TabTwo
   */
  await Promise.all([
    fetchRoutingTargetsAttrs(routingTargetsData.value, ['state', 'gtid_current_pos']),
    fetchListenersAttrs(listenersData.value),
    fetchFiltersAttrs(filtersData.value),
  ])
}

async function fetchTabTwoData() {
  await sessionsService.fetchSessions(filterSessionParam.value)
}

async function handlePatchRelationship({ type, data, isRoutingTargetType }) {
  await patchRelationship({
    id: obj_data.value.id,
    relationshipType: type,
    data,
    showSnackbar: !isRoutingTargetType,
    callback: async () => {
      await fetch()
      if (isRoutingTargetType) showCustomSnackbarMsg()
      switch (type) {
        case MXS_OBJ_TYPES.FILTERS:
          await fetchFiltersAttrs(filtersData.value)
          break
        case MXS_OBJ_TYPES.LISTENERS:
          await fetchListenersAttrs(listenersData.value)
          break
        default:
          await fetchRoutingTargetsAttrs(routingTargetsData.value, ['state', 'gtid_current_pos'])
          break
      }
    },
  })
}
/**
 * Mainly used for updating routing targets where
 * multiple relationship objects are updated.
 */
async function handlePatchRelationships(relationships) {
  const total = relationships.length
  for (const [i, { type, data }] of relationships.entries()) {
    let payload = {
      id: obj_data.value.id,
      relationshipType: type,
      data,
      showSnackbar: false,
    }
    if (i === total - 1)
      payload.callback = async () => {
        showCustomSnackbarMsg()
        await fetch()
        await fetchRoutingTargetsAttrs(routingTargetsData.value, ['state', 'gtid_current_pos'])
      }
    await patchRelationship(payload)
  }
}

function showCustomSnackbarMsg() {
  store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
    text: [`Successfully update routing targets of ${obj_data.value.id}`],
    type: 'success',
  })
}

const activeTab = computed(() =>
  activeTabIdx.value === 0
    ? {
        component: TabOne,
        props: {
          obj_data: obj_data.value,
          routingTargetItems: routingTargetItems.value,
          filterItems: filterItems.value,
          listenerItems: listenerItems.value,
          fetch,
          patchParams,
          handlePatchRelationship,
          handlePatchRelationships,
        },
      }
    : {
        component: TabTwo,
        props: {
          obj_data: obj_data.value,
          routingTargetItems: routingTargetItems.value,
          fetchSessions: async () => await sessionsService.fetchSessions(filterSessionParam.value),
        },
      }
)

async function onConfirmOp({ op, id }) {
  await opHandler({ op, id, callback: fetch })
}
</script>

<template>
  <ViewWrapper>
    <ViewHeader
      :item="obj_data"
      :type="MXS_OBJ_TYPES.SERVICES"
      showStateIcon
      :stateLabel="state"
      :operationMatrix="operationMatrix"
      :onConfirm="onConfirmOp"
      :onCountDone="fetchAll"
    />
    <VSheet v-if="!$helpers.lodash.isEmpty(obj_data)" class="pl-6">
      <OverviewBlocks :item="obj_data" ref="overviewBlocks" />
      <VTabs v-model="activeTabIdx">
        <VTab v-for="tab in TABS" :key="tab.name" class="text-primary"> {{ tab.name }} </VTab>
      </VTabs>
      <VWindow v-model="activeTabIdx">
        <VWindowItem v-for="(name, i) in TABS" :key="name" class="pt-5">
          <component v-if="i === activeTabIdx" :is="activeTab.component" v-bind="activeTab.props" />
        </VWindowItem>
      </VWindow>
    </VSheet>
  </ViewWrapper>
</template>
