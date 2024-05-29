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
import LazyInput from '@wsComps/DdlEditor/LazyInput.vue'
import erdHelper from '@/utils/erdHelper'
import {
  KEY_COL_EDITOR_ATTRS,
  KEY_COL_EDITOR_ATTRS_IDX_MAP,
  COL_ORDER_BY,
} from '@/constants/workspace'

const props = defineProps({
  modelValue: { type: Object, required: true },
  dim: { type: Object, required: true },
  keyId: { type: String, required: true },
  category: { type: String, required: true },
  tableColNameMap: { type: Object, required: true },
  tableColMap: { type: Object, required: true },
})
const emit = defineEmits(['update:modelValue'])
const typy = useTypy()
const {
  lodash: { keyBy, isEqual, cloneDeep },
  immutableUpdate,
} = useHelpers()

const IDX_OF_ID = KEY_COL_EDITOR_ATTRS_IDX_MAP[KEY_COL_EDITOR_ATTRS.ID]
const IDX_OF_ORDER_BY = KEY_COL_EDITOR_ATTRS_IDX_MAP[KEY_COL_EDITOR_ATTRS.ORDER_BY]
const IDX_OF_LENGTH = KEY_COL_EDITOR_ATTRS_IDX_MAP[KEY_COL_EDITOR_ATTRS.LENGTH]

const commonHeaderProps = { sortable: false, uppercase: true, useCellSlot: true }

const HEADERS = [
  { text: KEY_COL_EDITOR_ATTRS.ID, hidden: true },
  { text: KEY_COL_EDITOR_ATTRS.COL_ORDER, width: 50, minWidth: 50, ...commonHeaderProps },
  { text: KEY_COL_EDITOR_ATTRS.NAME, minWidth: 90, ...commonHeaderProps },
  { text: KEY_COL_EDITOR_ATTRS.TYPE, minWidth: 90, ...commonHeaderProps },
  {
    text: KEY_COL_EDITOR_ATTRS.ORDER_BY,
    width: 110,
    ...commonHeaderProps,
    cellProps: { class: 'px-1 d-inline-flex align-center justify-center' },
  },
  {
    text: KEY_COL_EDITOR_ATTRS.LENGTH,
    width: 80,
    ...commonHeaderProps,
    cellProps: { class: 'px-1 d-inline-flex align-center justify-center' },
  },
]
const ORDER_BY_ITEMS = Object.values(COL_ORDER_BY)

const selectedItems = ref([])
const stagingCategoryMap = ref({})
const rows = ref([])

const indexedCols = computed(
  () => typy(stagingCategoryMap.value[props.category], `${props.keyId}.cols`).safeArray
)
const indexedColMap = computed(() =>
  indexedCols.value.reduce((map, key, i) => {
    map[key.id] = { ...key, index: i }
    return map
  }, {})
)
const keyCategoryMap = computed({
  get: () => props.modelValue,
  set: (v) => emit('update:modelValue', v),
})
const rowMap = computed(() => keyBy(rows.value, (row) => row[IDX_OF_ID]))

// initialize with fresh data
watch(
  keyCategoryMap,
  (v) => {
    if (!isEqual(v, stagingCategoryMap.value)) init()
  },
  { deep: true }
)
watch(
  () => props.keyId,
  (v, oV) => {
    if (v !== oV) init()
  }
)
// sync changes to keyCategoryMap
watch(
  stagingCategoryMap,
  (v) => {
    if (!isEqual(keyCategoryMap.value, v)) keyCategoryMap.value = v
  },
  { deep: true }
)

watch(selectedItems, () => syncKeyCols(), { deep: true })
onBeforeMount(() => init())

function init() {
  stagingCategoryMap.value = cloneDeep(keyCategoryMap.value)
  setRows()
  setInitialSelectedItems()
}

function setRows() {
  rows.value = erdHelper
    .genIdxColOpts({ tableColMap: props.tableColMap })
    .map(({ id, text, type }) => {
      const indexedCol = indexedColMap.value[id]
      const order = typy(indexedCol, 'order').safeString || COL_ORDER_BY.ASC
      const length = typy(indexedCol, 'length').safeString || undefined
      return [id, '', text, type, order, length]
    })
}

function setInitialSelectedItems() {
  const ids = Object.keys(indexedColMap.value)
  selectedItems.value = ids.map((id) => rowMap.value[id])
}

function getColOrder(rowData) {
  const id = rowData[IDX_OF_ID]
  const indexedCol = indexedColMap.value[id]
  return indexedCol ? indexedCol.index : ''
}

function onChangeInput({ rowIdx, rowData, colIdx, value }) {
  // Update component state
  rows.value = immutableUpdate(rows.value, {
    [rowIdx]: { [colIdx]: { $set: value } },
  })
  const colId = rowData[IDX_OF_ID]
  const indexedCol = indexedColMap.value[colId]
  // Update selectedItems
  if (indexedCol) {
    const selectedColIdx = selectedItems.value.findIndex((item) => item[IDX_OF_ID] === colId)
    if (selectedColIdx >= 0)
      selectedItems.value = immutableUpdate(selectedItems.value, {
        [selectedColIdx]: { [colIdx]: { $set: value } },
      })
  }
}

function onRowClick(rowData) {
  const index = selectedItems.value.findIndex((item) => item[IDX_OF_ID] === rowData[IDX_OF_ID])
  if (index >= 0) selectedItems.value.splice(index, 1)
  else selectedItems.value.push(rowData)
}

/**
 * Keeps selectedItems them in sync with indexedCols
 */
function syncKeyCols() {
  const cols = selectedItems.value.reduce((acc, item) => {
    const id = item[IDX_OF_ID]
    const order = item[IDX_OF_ORDER_BY]
    const length = item[IDX_OF_LENGTH]
    let col = { id }
    if (order !== COL_ORDER_BY.ASC) col.order = order
    if (length > 0) col.length = length
    acc.push(col)
    return acc
  }, [])

  stagingCategoryMap.value = immutableUpdate(stagingCategoryMap.value, {
    [props.category]: { [props.keyId]: { cols: { $set: cols } } },
  })
}
</script>

<template>
  <VirtualScrollTbl
    v-model:selectedItems="selectedItems"
    :key="keyId"
    :headers="HEADERS"
    :data="rows"
    :itemHeight="32"
    :maxHeight="dim.height"
    :boundingWidth="dim.width"
    showSelect
    :style="{ maxWidth: `${dim.width}px` }"
    :showRowCount="false"
    @row-click="onRowClick"
  >
    <template #[KEY_COL_EDITOR_ATTRS.COL_ORDER]="{ data: { rowData } }">
      {{ getColOrder(rowData) }}
    </template>
    <template #[KEY_COL_EDITOR_ATTRS.ORDER_BY]="{ data: { cell, rowIdx, colIdx, rowData } }">
      <LazyInput
        :modelValue="cell"
        isSelect
        :items="ORDER_BY_ITEMS"
        @update:modelValue="onChangeInput({ value: $event, rowIdx, rowData, colIdx })"
      />
    </template>
    <template #[KEY_COL_EDITOR_ATTRS.LENGTH]="{ data: { cell, rowIdx, colIdx, rowData } }">
      <LazyInput
        :modelValue="cell"
        @update:modelValue="onChangeInput({ value: $event, rowIdx, rowData, colIdx })"
        @blur="
          onChangeInput({
            value: $typy($event, 'srcElement.value').safeString,
            rowIdx,
            rowData,
            colIdx,
          })
        "
        @keypress="$helpers.preventNonNumericalVal($event)"
      />
    </template>
  </VirtualScrollTbl>
</template>
