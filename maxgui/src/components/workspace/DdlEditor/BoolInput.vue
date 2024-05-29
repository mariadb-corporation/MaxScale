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
import { checkUniqueZeroFillSupport, checkAutoIncrementSupport } from '@wsComps/DdlEditor/utils'
import { COL_ATTRS, COL_ATTRS_IDX_MAP, GENERATED_TYPES } from '@/constants/workspace'

const props = defineProps({
  modelValue: { type: Boolean, required: true },
  rowData: { type: Array, required: true },
  field: { type: String, required: true },
})

const emit = defineEmits(['update:modelValue'])

const typy = useTypy()

const { TYPE, PK, NN, UN, UQ, ZF, AI, GENERATED } = COL_ATTRS
const colData = computed(() => ({
  type: typy(props.rowData, `[${COL_ATTRS_IDX_MAP[TYPE]}]`).safeString,
  isPK: typy(props.rowData, `[${COL_ATTRS_IDX_MAP[PK]}]`).safeBoolean,
  isAI: typy(props.rowData, `[${COL_ATTRS_IDX_MAP[AI]}]`).safeBoolean,
  isGenerated:
    typy(props.rowData, `[${COL_ATTRS_IDX_MAP[GENERATED]}]`).safeString !== GENERATED_TYPES.NONE,
}))

const isDisabled = computed(() => {
  switch (props.field) {
    case PK:
      //disable if column is generated
      return colData.value.isGenerated
    case UN:
    case ZF:
      return !checkUniqueZeroFillSupport(colData.value.type)
    case AI:
      return !checkAutoIncrementSupport(colData.value.type)
    case NN:
      // isAI or isPK implies NOT NULL so must be disabled
      // when column is generated, NN or NULL can not be defined
      return colData.value.isAI || colData.value.isPK || colData.value.isGenerated
    case UQ:
      return colData.value.isPK // implies UNIQUE already so UQ must be disabled
    default:
      return false
  }
})
</script>

<template>
  <div
    v-ripple
    class="d-flex align-center justify-center checkbox-btn rounded-circle"
    :class="[isDisabled ? 'disabled pointer-events--none cursor--default' : 'cursor--pointer']"
    @click="emit('update:modelValue', !modelValue)"
  >
    <VIcon
      size="18"
      :color="modelValue && !isDisabled ? 'primary' : 'text-subtle'"
      :icon="modelValue ? '$checkboxOn' : '$checkboxOff'"
    />
  </div>
</template>

<style lang="scss" scoped>
.checkbox-btn {
  width: 28px;
  height: 28px;
  &:hover {
    &::before {
      opacity: 0.2;
      content: '';
      position: absolute;
      width: 28px;
      height: 28px;
      border-radius: 100%;
      background-color: currentColor;
      pointer-events: none;
    }
  }
  &.disabled {
    opacity: 0.4;
  }
}
</style>
