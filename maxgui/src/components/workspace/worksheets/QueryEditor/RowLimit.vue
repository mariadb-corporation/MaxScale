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
import { SNACKBAR_TYPE_MAP } from '@/constants'
import { DEF_ROW_LIMIT_OPTS, NO_LIMIT } from '@/constants/workspace'

const props = defineProps({
  modelValue: { type: [Number, String] },
  minimized: { type: Boolean, default: false },
  borderless: { type: Boolean, default: false },
  showErrInSnackbar: { type: Boolean, default: false },
  hasNoLimit: { type: Boolean, default: false },
  allowEmpty: { type: Boolean, default: false },
})

const emit = defineEmits(['update:modelValue'])

const store = useStore()
const typy = useTypy()
const { t } = useI18n()
const { validateRequired, validatePositiveNum } = useValidationRule()

const rowLimitValidity = ref(true)
const input = ref(10000)

const items = computed(() =>
  props.hasNoLimit ? [NO_LIMIT, ...DEF_ROW_LIMIT_OPTS] : DEF_ROW_LIMIT_OPTS
)

watch(input, (v) => {
  const res = validate(v)
  rowLimitValidity.value = res === true
  if (props.showErrInSnackbar && typy(res).isString)
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', { text: [res], type: SNACKBAR_TYPE_MAP.ERROR })
  if (rowLimitValidity.value && v !== props.modelValue) {
    emit('update:modelValue', v)
    if (v === NO_LIMIT)
      store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
        text: [t('warnings.noLimit')],
        type: SNACKBAR_TYPE_MAP.WARNING,
      })
  }
})

watch(
  () => props.modelValue,
  (v) => {
    if (v !== input.value) setInputValue()
  },
  { immediate: true }
)

function validate(v) {
  if (typy(v).isNumber) return validatePositiveNum(v)
  if (typy(v).isNull) return props.allowEmpty ? true : validateRequired(v)
  if (props.hasNoLimit && v === NO_LIMIT) return true
  return t('errors.negativeNum')
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
      'borderless-input': borderless && rowLimitValidity,
    }"
    :min-width="100"
    :items="items"
    :rules="[validate]"
    @keypress="$helpers.preventNonNumericalVal($event)"
  />
</template>

<style lang="scss" scoped>
.row-limit {
  :deep(.v-input__control) {
    .v-text-field__prefix {
      padding-left: 12px;
      color: colors.$small-text;
      font-size: 0.875rem;
    }
    .v-field__input {
      padding: 0 0 0 8px !important;
    }
  }
}
</style>
