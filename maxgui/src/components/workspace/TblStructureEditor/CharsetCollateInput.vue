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
import LazyInput from '@wsComps/TblStructureEditor/LazyInput.vue'
import CharsetCollateSelect from '@wsComps/TblStructureEditor/CharsetCollateSelect.vue'
import { checkCharsetSupport } from '@wsComps/TblStructureEditor/utils'
import { CREATE_TBL_TOKEN_MAP, COL_ATTR_MAP, COL_ATTR_IDX_MAP } from '@/constants/workspace'

defineOptions({ inheritAttrs: false })
const props = defineProps({
  rowData: { type: Array, required: true },
  field: { type: String, required: true },
  charsetCollationMap: { type: Object, required: true },
  defTblCharset: { type: String, default: '' },
  defTblCollation: { type: String, default: '' },
})
const attrs = useAttrs()

const typy = useTypy()

const columnCharset = computed(
  () =>
    typy(props.rowData, `[${COL_ATTR_IDX_MAP[COL_ATTR_MAP.CHARSET]}]`).safeString ||
    props.defTblCharset
)
const columnType = computed(
  () => typy(props.rowData, `[${COL_ATTR_IDX_MAP[COL_ATTR_MAP.TYPE]}]`).safeString
)
const isCharsetInput = computed(() => props.field === COL_ATTR_MAP.CHARSET)
const isDisabled = computed(
  () => columnType.value.includes('NATIONAL') || !checkCharsetSupport(columnType.value)
)
const defItem = computed(() => (isCharsetInput.value ? props.defTblCharset : props.defTblCollation))
const items = computed(() =>
  isCharsetInput.value
    ? Object.keys(props.charsetCollationMap)
    : typy(props.charsetCollationMap, `[${columnCharset.value}].collations`).safeArray
)
const placeholder = computed(() => (isDisabled.value ? '' : CREATE_TBL_TOKEN_MAP.default))
const modelValue = computed(() => (attrs.modelValue === '' ? null : attrs.modelValue))
</script>

<template>
  <template v-if="!isDisabled">
    <LazyInput
      :modelValue="modelValue"
      :items="items"
      :disabled="isDisabled"
      persistent-placeholder
      :placeholder="placeholder"
      isSelect
      useCustomInput
      v-bind="$attrs"
    >
      <template #default="{ props }">
        <CharsetCollateSelect :defItem="defItem" v-bind="props" />
      </template>
    </LazyInput>
  </template>
  <span v-else />
</template>
