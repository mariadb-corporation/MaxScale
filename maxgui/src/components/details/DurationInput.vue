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
import ParameterInput from '@/components/common/ParametersTable/ParameterInput.vue'
const props = defineProps({
  modelValue: { type: String, required: true }, // e.g. 1ms 1s 1m 1h
})
const emit = defineEmits(['update:modelValue'])

const keyInfo = ref({ mandatory: true, modifiable: true, type: 'duration', unit: 'm' })
const item = ref({ id: 'timeout', key: 'timeout', value: props.modelValue })

function onChangeParam(v) {
  emit('update:modelValue', v.value)
}
</script>

<template>
  <div class="duration-dropdown">
    <label class="d-block label-field text-small-text label--required">
      {{ $t('timeout') }}
    </label>
    <ParameterInput
      :item="item"
      :keyInfo="keyInfo"
      creationMode
      :isListener="false"
      @on-change="onChangeParam"
    />
  </div>
</template>
