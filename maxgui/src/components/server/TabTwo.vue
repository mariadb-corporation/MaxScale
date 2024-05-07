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
  obj_data: { type: Object, required: true },
  patchParams: { type: Function, required: true },
  fetch: { type: Function, required: true },
})
const store = useStore()
const route = useRoute()
const typy = useTypy()

const monitor_diagnostics = computed(() => store.state.monitors.monitor_diagnostics)
const module_parameters = computed(() => store.state.module_parameters)
const monitorDiagnostics = computed(
  () =>
    typy(monitor_diagnostics.value, 'server_info').safeArray.find(
      (server) => server.name === route.params.id
    ) || {}
)

async function updateParams(data) {
  await props.patchParams({ id: props.obj_data.id, data, callback: props.fetch })
}
</script>

<template>
  <VRow>
    <VCol cols="6">
      <ParametersTable
        :data="obj_data.attributes.parameters"
        :paramsInfo="module_parameters"
        :confirmEdit="updateParams"
        :mxsObjType="MXS_OBJ_TYPES.SERVERS"
      />
    </VCol>
    <VCol cols="6">
      <CollapsibleReadOnlyTbl
        :title="`${$t('monitorDiagnostics')}`"
        :data="monitorDiagnostics"
        expandAll
      />
    </VCol>
  </VRow>
</template>
