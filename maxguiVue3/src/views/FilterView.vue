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
import { MXS_OBJ_TYPES } from '@/constants'
import PageHeader from '@/components/details/PageHeader.vue'
import RelationshipTable from '@/components/details/RelationshipTable.vue'

const store = useStore()
const route = useRoute()
const typy = useTypy()
const { map: commonOps, handler: opHandler } = useCommonObjOpMap(MXS_OBJ_TYPES.FILTERS)
const { fetchObj, patchParams } = useMxsObjActions(MXS_OBJ_TYPES.FILTERS)
const { items: serviceItems, fetch: fetchServicesAttrs } = useObjRelationshipData()
const obj_data = computed(() => store.state.filters.obj_data)
const operationMatrix = computed(() => [Object.values(commonOps)])
const module_parameters = computed(() => store.state.module_parameters)
const filter_diagnostics = computed(
  () => typy(obj_data.value, 'attributes.filter_diagnostics').safeObjectOrEmpty
)

// re-fetch when the route changes
watch(
  () => route.path,
  async () => await initialFetch()
)
onBeforeMount(async () => await initialFetch())

async function fetch() {
  await fetchObj(route.params.id)
}

async function initialFetch() {
  await fetch()
  // wait until get obj_data to fetch service state  and module parameters
  const {
    attributes: { module: filterModule = null } = {},
    relationships: { services: { data: servicesData = [] } = {} } = {},
  } = obj_data.value

  await fetchServicesAttrs(servicesData)
  if (filterModule) await store.dispatch('fetchModuleParameters', filterModule)
}

async function updateParams(data) {
  await patchParams({ id: obj_data.value.id, data, callback: fetch })
}
</script>

<template>
  <ViewWrapper>
    <PageHeader
      :item="obj_data"
      :type="MXS_OBJ_TYPES.FILTERS"
      :operationMatrix="operationMatrix"
      :onConfirm="opHandler"
    >
    </PageHeader>
    <VSheet v-if="!$helpers.lodash.isEmpty(obj_data)" class="pl-6 pt-3">
      <VRow>
        <VCol cols="6">
          <ParametersTable
            :data="obj_data.attributes.parameters"
            :paramsInfo="module_parameters"
            :confirmEdit="updateParams"
            :mxsObjType="MXS_OBJ_TYPES.FILTERS"
          />
        </VCol>
        <VCol cols="6">
          <VRow>
            <VCol cols="12">
              <RelationshipTable :type="MXS_OBJ_TYPES.SERVICES" :data="serviceItems" />
            </VCol>
            <VCol v-if="!$typy(filter_diagnostics).isEmptyObject" cols="12">
              <CollapsibleReadOnlyTbl
                :title="`${$t('diagnostics', 2)}`"
                :data="filter_diagnostics"
                expandAll
              />
            </VCol>
          </VRow>
        </VCol>
      </VRow>
    </VSheet>
  </ViewWrapper>
</template>
