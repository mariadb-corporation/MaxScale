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
defineOptions({ inheritAttrs: false })
const props = defineProps({
  modelValue: { type: String },
  type: { type: String, required: true },
  allObjIds: { type: Array, required: true },
})
const emit = defineEmits(['update:modelValue'])

const { t } = useI18n()

let objId = computed({
  get: () => props.modelValue,
  set: (v) => emit('update:modelValue', v),
})

watch(objId, async (v) => {
  // add hyphens when ever input have whitespace
  objId.value = v ? v.split(' ').join('-') : v
})

function validateResourceId(val) {
  if (!val) return t('errors.requiredInput', { inputName: 'id' })
  else if (props.allObjIds.includes(val)) return t('errors.duplicatedValue')
  return true
}
</script>

<template>
  <label class="field__label text-small-text d-block label-required" for="obj-id">
    {{ $t('mxsObjLabelName', { type: $t(type, 1) }) }}
  </label>
  <VTextField
    v-model="objId"
    :rules="[(v) => validateResourceId(v)]"
    name="id"
    required
    :placeholder="$t('nameYour', { type: $t(type, 1).toLowerCase() })"
    hide-details="auto"
    id="obj-id"
    v-bind="$attrs"
  />
</template>
