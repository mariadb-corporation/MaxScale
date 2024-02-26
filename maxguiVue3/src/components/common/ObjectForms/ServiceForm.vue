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

const props = defineProps({
  allFilters: { type: Array, required: true },
  defRoutingTargetItems: { type: Array, default: () => [] },
  defFilterItem: { type: Array, default: () => [] },
  moduleParamsProps: { type: Object, required: true },
})

const typy = useTypy()

let routingTargetItems = ref([])
let selectedFilters = ref([])
let moduleId = ref('')
let changedParams = ref({})

const filtersList = computed(() => props.allFilters.map(({ id, type }) => ({ id, type })))

const routingTargetRelationships = computed(() => {
  let data = routingTargetItems.value
  if (typy(routingTargetItems.value).isObject) data = [routingTargetItems.value]
  return data.reduce((obj, item) => {
    if (!obj[item.type]) obj[item.type] = { data: [] }
    obj[item.type].data.push(item)
    return obj
  }, {})
})

function getValues() {
  return {
    moduleId: moduleId.value,
    parameters: changedParams.value,
    relationships: {
      filters: { data: selectedFilters.value },
      ...routingTargetRelationships.value,
    },
  }
}
defineExpose({ getValues, routingTargetItems, filtersList })
</script>

<template>
  <div class="mb-2">
    <ModuleParameters
      moduleName="router"
      :mxsObjType="MXS_OBJ_TYPES.SERVICES"
      @get-module-id="moduleId = $event"
      @get-changed-params="changedParams = $event"
      v-bind="moduleParamsProps"
    />
    <CollapsibleCtr class="mt-4" titleWrapperClass="mx-n9" :title="$t('routingTargets')">
      <RoutingTargetSelect v-model="routingTargetItems" :initialValue="defRoutingTargetItems" />
    </CollapsibleCtr>
    <ResourceRelationships
      type="filters"
      :items="filtersList"
      :initialValue="defFilterItem"
      clearable
      multiple
      @get-values="selectedFilters = $event"
    />
  </div>
</template>
