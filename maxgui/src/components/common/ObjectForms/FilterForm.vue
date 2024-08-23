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
import { MXS_OBJ_TYPE_MAP } from '@/constants'

defineProps({ moduleParamsProps: { type: Object, required: true } })

const moduleId = ref('')
const changedParams = ref({})

function getValues() {
  return { attributes: { module: moduleId.value, parameters: changedParams.value } }
}
defineExpose({ getValues })
</script>

<template>
  <div class="mb-2">
    <ModuleParameters
      moduleName="module"
      :mxsObjType="MXS_OBJ_TYPE_MAP.FILTERS"
      @get-module-id="moduleId = $event"
      @get-changed-params="changedParams = $event"
      v-bind="moduleParamsProps"
    />
  </div>
</template>
