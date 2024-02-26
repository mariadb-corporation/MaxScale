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

defineProps({ moduleParamsProps: { type: Object, required: true } })

let moduleId = ref('')
let changedParams = ref({})

function getValues() {
  return { moduleId: moduleId.value, parameters: changedParams.value }
}
defineExpose({ getValues })
</script>

<template>
  <div class="mb-2">
    <ModuleParameters
      moduleName="module"
      :mxsObjType="MXS_OBJ_TYPES.FILTERS"
      @get-module-id="moduleId = $event"
      @get-changed-params="changedParams = $event"
      v-bind="moduleParamsProps"
    />
  </div>
</template>
