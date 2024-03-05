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
defineOptions({
  inheritAttrs: false,
})
const props = defineProps({
  modelValue: { type: String, required: true },
  label: { type: String, required: true },
  customErrMsg: { type: String, default: '' },
})
const emit = defineEmits(['update:modelValue'])
const attrs = useAttrs()

const {
  lodash: { uniqueId },
} = useHelpers()
const typy = useTypy()
const { t } = useI18n()

const input = computed({
  get: () => props.modelValue,
  set: (v) => emit('update:modelValue', v),
})
const id = computed(() => attrs.id || `label-field-${uniqueId()}`)

const customRules = computed(() => typy(attrs, 'rules').safeArray)

const rules = computed(() => {
  if (customRules.value.length) return customRules.value
  return [
    (val) => !!val || props.customErrMsg || t('errors.requiredInput', { inputName: props.label }),
  ]
})
</script>

<template>
  <label
    class="field__label text-small-text"
    :class="{ 'label-required': $attrs.required }"
    :for="id"
  >
    {{ label }}
  </label>
  <VTextField
    v-model="input"
    :id="id"
    hide-details="auto"
    :rules="$attrs.required ? rules : []"
    v-bind="$attrs"
  >
    <template v-for="(_, name) in $slots" #[name]="slotData">
      <slot :name="name" v-bind="slotData" />
    </template>
  </VTextField>
</template>
