<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import LazyInput from '@wsComps/DdlEditor/LazyInput.vue'
import {
  CREATE_TBL_TOKENS,
  NON_FK_CATEGORIES,
  KEY_EDITOR_ATTRS,
  KEY_EDITOR_ATTR_IDX_MAP,
} from '@/constants/workspace'

const props = defineProps({
  modelValue: { type: Object, required: true },
  dim: { type: Object, required: true },
  selectedItems: { type: Array, default: () => [] },
})
const emit = defineEmits(['update:modelValue', 'update:selectedItems'])

const IDX_OF_ID = KEY_EDITOR_ATTR_IDX_MAP[KEY_EDITOR_ATTRS.ID]
const IDX_OF_CATEGORY = KEY_EDITOR_ATTR_IDX_MAP[KEY_EDITOR_ATTRS.CATEGORY]
const {
  lodash: { isEqual, cloneDeep },
  immutableUpdate,
} = useHelpers()
const typy = useTypy()

const commonHeaderProps = {
  sortable: false,
  uppercase: true,
  useCellSlot: true,
  cellProps: { class: 'px-1 d-inline-flex align-center justify-center' },
}

const HEADERS = [
  { text: KEY_EDITOR_ATTRS.ID, hidden: true },
  { text: KEY_EDITOR_ATTRS.NAME, required: true, ...commonHeaderProps },
  { text: KEY_EDITOR_ATTRS.CATEGORY, width: 142, required: true, ...commonHeaderProps },
  { text: KEY_EDITOR_ATTRS.COMMENT, width: 200, ...commonHeaderProps },
]

const keyItems = ref([])
const stagingCategoryMap = ref({})

const keyCategoryMap = computed({
  get: () => props.modelValue,
  set: (v) => emit('update:modelValue', v),
})
const selectedRows = computed({
  get: () => props.selectedItems,
  set: (v) => emit('update:selectedItems', v),
})

const hasPk = computed(() => Boolean(stagingCategoryMap.value[CREATE_TBL_TOKENS.primaryKey]))
const categories = computed(() =>
  Object.values(NON_FK_CATEGORIES).map((item) => ({
    text: categoryTxt(item),
    value: item,
    disabled: item === CREATE_TBL_TOKENS.primaryKey && hasPk.value,
  }))
)

watch(
  keyCategoryMap,
  (v) => {
    if (!isEqual(v, stagingCategoryMap.value)) assignData()
  },
  { deep: true }
)
watch(
  stagingCategoryMap,
  (v) => {
    if (!isEqual(keyCategoryMap.value, v)) keyCategoryMap.value = v
  },
  { deep: true }
)

onBeforeMount(() => init())

function init() {
  assignData()
  handleSelectItem(0)
}

function assignData() {
  stagingCategoryMap.value = cloneDeep(keyCategoryMap.value)
  const { foreignKey, primaryKey } = CREATE_TBL_TOKENS
  keyItems.value = Object.values(NON_FK_CATEGORIES).reduce((acc, category) => {
    if (category !== foreignKey) {
      const keys = Object.values(stagingCategoryMap.value[category] || {})
      acc = [
        ...acc,
        ...keys.map(({ id, name = categoryTxt(primaryKey), comment = '' }) => [
          id,
          name,
          category,
          comment,
        ]),
      ]
    }
    return acc
  }, [])
}

function handleSelectItem(idx) {
  if (keyItems.value.length) selectedRows.value = [keyItems.value.at(idx)]
}

function categoryTxt(category) {
  if (category === CREATE_TBL_TOKENS.key) return 'INDEX'
  return category.replace('KEY', '')
}

function isInputRequired(field) {
  return field !== KEY_EDITOR_ATTRS.COMMENT
}

function isInputDisabled({ field, rowData }) {
  const category = rowData[IDX_OF_CATEGORY]
  return category === CREATE_TBL_TOKENS.primaryKey && field !== KEY_EDITOR_ATTRS.COMMENT
}

function onChangeInput({ field, rowData, rowIdx, colIdx, value }) {
  const category = rowData[IDX_OF_CATEGORY]
  const keyId = rowData[IDX_OF_ID]

  let currKeyMap = stagingCategoryMap.value[category] || {}
  const clonedKey = cloneDeep(currKeyMap[keyId])

  switch (field) {
    case KEY_EDITOR_ATTRS.NAME:
      stagingCategoryMap.value = immutableUpdate(stagingCategoryMap.value, {
        [category]: { [keyId]: { name: { $set: value } } },
      })
      break
    case KEY_EDITOR_ATTRS.COMMENT:
      stagingCategoryMap.value = immutableUpdate(stagingCategoryMap.value, {
        [category]: {
          [keyId]: value ? { comment: { $set: value } } : { $unset: ['comment'] },
        },
      })
      break
    case KEY_EDITOR_ATTRS.CATEGORY: {
      currKeyMap = immutableUpdate(currKeyMap, { $unset: [keyId] })
      const newCategory = value
      let keyCategoryMap = immutableUpdate(
        stagingCategoryMap.value,
        Object.keys(currKeyMap).length
          ? { $merge: { [category]: currKeyMap } }
          : { $unset: [category] }
      )
      const newCategoryKeyMap = keyCategoryMap[newCategory] || {}
      keyCategoryMap = immutableUpdate(keyCategoryMap, {
        $merge: {
          [newCategory]: {
            ...newCategoryKeyMap,
            [clonedKey.id]: clonedKey,
          },
        },
      })
      stagingCategoryMap.value = keyCategoryMap
      break
    }
  }

  // Update component states
  keyItems.value = immutableUpdate(keyItems.value, {
    [rowIdx]: { [colIdx]: { $set: value } },
  })

  /* TODO: Resolve the current limitation in VirtualScrollTbl.
   * The `selectedItems` props stores the entire row data, so that
   * the styles for selected rows can be applied.
   * The id of the row should have been used to facilitate the edit
   * feature. For now, selectedRows must be also updated.
   */
  const isUpdatingIdxRow = typy(selectedRows.value, `[0][${IDX_OF_ID}]`).safeString === keyId
  if (isUpdatingIdxRow)
    selectedRows.value = immutableUpdate(selectedRows.value, {
      [0]: { [colIdx]: { $set: value } },
    })
}

function onRowClick(rowData) {
  selectedRows.value = [rowData]
}
defineExpose({ handleSelectItem })
</script>

<template>
  <VirtualScrollTbl
    v-model:selectedItems="selectedRows"
    :headers="HEADERS"
    :data="keyItems"
    :itemHeight="32"
    :maxHeight="dim.height"
    :boundingWidth="dim.width"
    showSelect
    singleSelect
    :noDataText="$t('noEntity', { entityName: $t('indexes') })"
    :style="{ maxWidth: `${dim.width}px` }"
    @row-click="onRowClick"
  >
    <template
      v-for="h in HEADERS"
      #[h.text]="{ data: { cell, rowIdx, colIdx, rowData } }"
      :key="h.text"
    >
      <LazyInput
        v-if="h.text === KEY_EDITOR_ATTRS.CATEGORY"
        :modelValue="cell"
        isSelect
        item-title="text"
        :items="categories"
        :disabled="isInputDisabled({ field: h.text, rowData })"
        :selectionText="categoryTxt(cell)"
        @update:modelValue="
          onChangeInput({ value: $event, field: h.text, rowIdx, colIdx, rowData })
        "
        @focus="onRowClick(rowData)"
      />

      <LazyInput
        v-else
        :modelValue="cell"
        :required="isInputRequired(h.text)"
        :disabled="isInputDisabled({ field: h.text, rowData })"
        @update:modelValue="
          onChangeInput({ value: $event, field: h.text, rowIdx, colIdx, rowData })
        "
        @blur="
          onChangeInput({
            value: $typy($event, 'srcElement.value').safeString,
            field: h.text,
            rowIdx,
            colIdx,
            rowData,
          })
        "
        @focus="onRowClick(rowData)"
      />
    </template>
  </VirtualScrollTbl>
</template>
