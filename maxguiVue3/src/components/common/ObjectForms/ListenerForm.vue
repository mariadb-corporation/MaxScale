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

import { MXS_OBJ_TYPES, MRDB_PROTOCOL } from '@/constants'

const props = defineProps({
  allServices: { type: Array, required: true },
  defaultItems: { type: [Array, Object], default: () => [] },
  moduleParamsProps: { type: Object, required: true },
})

let selectedServices = ref([])
let moduleId = ref('')
let changedParams = ref({})

//  several listeners may be associated with the same service, so list all current services
const servicesList = computed(() => props.allServices.map(({ id, type }) => ({ id, type })))

function getValues() {
  return {
    parameters: { ...changedParams.value, protocol: moduleId.value },
    relationships: {
      services: { data: selectedServices.value },
    },
  }
}
defineExpose({ getValues, servicesList })
</script>
<template>
  <div class="mb-2">
    <ModuleParameters
      moduleName="protocol"
      :defModuleId="MRDB_PROTOCOL"
      :mxsObjType="MXS_OBJ_TYPES.LISTENERS"
      @get-module-id="moduleId = $event"
      @get-changed-params="changedParams = $event"
      v-bind="moduleParamsProps"
    />
    <!-- A listener may be associated with a single service, so multiple props is set to false-->
    <ResourceRelationships
      type="services"
      :items="servicesList"
      :initialValue="defaultItems"
      required
      @get-values="selectedServices = $event"
    />
  </div>
</template>
