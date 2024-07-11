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
  modelValue: { type: Number },
  minimized: { type: Boolean, default: false },
})

const emit = defineEmits(['update:modelValue'])
const typy = useTypy()
const { t } = useI18n()

const inputValue = computed({
  get: () => props.modelValue,
  set(v) {
    if (typy(v).isNumber && typy(v).safeNumber > 0) emit('update:modelValue', v)
  },
})

function validate(value) {
  const v = Number(value)
  if (v <= 0) return t('errors.largerThanZero', { inputName: 'Value' })
  else if (!typy(v).isNumber) return t('errors.nonInteger')
  return true
}
</script>

<template>
  <VCombobox
    v-model.number="inputValue"
    class="row-limit"
    :class="{ 'minimized-input': minimized }"
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
