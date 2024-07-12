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
import { DEF_ROW_LIMIT_OPTS } from '@/constants/workspace'

const props = defineProps({
  modelValue: { type: [Number, String] },
  minimized: { type: Boolean, default: false },
  borderless: { type: Boolean, default: false },
  showErrInSnackbar: { type: Boolean, default: false },
  validateFn: { type: Function },
})

const emit = defineEmits(['update:modelValue'])

const store = useStore()
const typy = useTypy()
const { t } = useI18n()

const rowLimitValidity = ref(true)
const input = ref(10000)

watch(input, (v) => {
  const res = validate(v)
  rowLimitValidity.value = res === true
  if (props.showErrInSnackbar && typy(res).isString)
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', { text: [res], type: 'error' })
  if (rowLimitValidity.value && v !== props.modelValue) emit('update:modelValue', v)
})

watch(
  () => props.modelValue,
  (v) => {
    if (v !== input.value) setInputValue()
  },
  { immediate: true }
)

function validate(v) {
  if (typy(props.validateFn).isFunction) return props.validateFn(v)
  if (typy(v).isNumber) return v >= 1 ? true : t('errors.largerThanZero', { inputName: 'Value' })
  if (typy(v).isNull) return t('errors.requiredInput', { inputName: 'Value' })
  return t('errors.nonInteger')
}

function setInputValue() {
  input.value = props.modelValue
}
</script>

<template>
  <VCombobox
    v-model.number="input"
    class="row-limit"
    :class="{
      'minimized-input': minimized,
      'v-combobox--borderless': borderless && rowLimitValidity,
    }"
    :min-width="100"
    :items="DEF_ROW_LIMIT_OPTS"
    :rules="[(v) => validate(v)]"
    @keypress="$helpers.preventNonNumericalVal($event)"
  />
</template>

<style lang="scss" scoped>
.row-limit:deep(.v-text-field__prefix) {
  padding-left: 12px;
  color: colors.$small-text;
  font-size: 0.875rem;
}
</style>
