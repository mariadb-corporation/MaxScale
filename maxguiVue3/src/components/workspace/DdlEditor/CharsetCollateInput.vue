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
import CharsetCollateSelect from '@/components/workspace/DdlEditor/CharsetCollateSelect.vue'
import { checkCharsetSupport } from '@/components/workspace/DdlEditor/utils'
import { CREATE_TBL_TOKENS, COL_ATTRS, COL_ATTRS_IDX_MAP } from '@/constants/workspace'

const props = defineProps({
  modelValue: { type: String, required: true },
  rowData: { type: Array, required: true },
  field: { type: String, required: true },
  charsetCollationMap: { type: Object, required: true },
  defTblCharset: { type: String, default: '' },
  defTblCollation: { type: String, default: '' },
})
const emit = defineEmits(['update:modelValue'])

const typy = useTypy()

const inputValue = computed({
  get() {
    return props.modelValue
  },
  set(v) {
    if (v !== inputValue.value) emit('update:modelValue', v)
  },
})

const columnCharset = computed(
  () =>
    typy(props.rowData, `[${COL_ATTRS_IDX_MAP[COL_ATTRS.CHARSET]}]`).safeString ||
    props.defTblCharset
)

const columnType = computed(
  () => typy(props.rowData, `[${COL_ATTRS_IDX_MAP[COL_ATTRS.TYPE]}]`).safeString
)

const isCharsetInput = computed(() => props.field === COL_ATTRS.CHARSET)

const isDisabled = computed(
  () => columnType.value.includes('NATIONAL') || !checkCharsetSupport(columnType.value)
)

const defItem = computed(() => (isCharsetInput.value ? props.defTblCharset : props.defTblCollation))
</script>

<template>
  <CharsetCollateSelect
    v-model="inputValue"
    :items="
      isCharsetInput
        ? Object.keys(charsetCollationMap)
        : $typy(charsetCollationMap, `[${columnCharset}].collations`).safeArray
    "
    :defItem="defItem"
    :disabled="isDisabled"
    :placeholder="CREATE_TBL_TOKENS.default"
  />
</template>
