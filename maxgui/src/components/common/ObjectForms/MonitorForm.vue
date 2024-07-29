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
import { MXS_OBJ_TYPE_MAP, MRDB_MON } from '@/constants'

const props = defineProps({
  allServers: { type: Array, required: true },
  defaultItems: { type: Array, default: () => [] },
  moduleParamsProps: { type: Object, required: true },
})

const selectedServers = ref([])
const moduleId = ref('')
const changedParams = ref({})

const serversList = computed(() => {
  const serverItems = []
  // get only server that are not monitored
  props.allServers.forEach(({ id, type, relationships: { monitors = null } = {} }) => {
    if (!monitors) serverItems.push({ id, type })
  })
  return serverItems
})

const defServers = computed(() =>
  serversList.value.filter((item) => props.defaultItems.some((defItem) => defItem.id === item.id))
)

function getValues() {
  return {
    attributes: { module: moduleId.value, parameters: changedParams.value },
    relationships: { servers: { data: selectedServers.value } },
  }
}
defineExpose({ getValues, serversList, defServers })
</script>

<template>
  <div class="mb-2">
    <ModuleParameters
      moduleName="module"
      :defModuleId="MRDB_MON"
      :mxsObjType="MXS_OBJ_TYPE_MAP.MONITORS"
      @get-module-id="moduleId = $event"
      @get-changed-params="changedParams = $event"
      v-bind="moduleParamsProps"
    />
    <ResourceRelationships
      type="servers"
      multiple
      :items="serversList"
      :initialValue="defServers"
      clearable
      @get-values="selectedServers = $event"
    />
  </div>
</template>
