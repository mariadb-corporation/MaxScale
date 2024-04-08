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
const props = defineProps({
  collapsedRowGroups: { type: Array, required: true },
  selectedItems: { type: Array, required: true },
  selectedGroupRows: { type: Array, required: true },
  row: { type: Object, required: true },
  tableData: { type: Array, required: true },
  isCollapsed: { type: Boolean, required: true },
  boundingWidth: { type: Number, required: true },
  lineHeight: { type: String, required: true },
  showSelect: { type: Boolean, required: true },
  maxWidth: { type: Number, required: true },
  filterByColIndexes: { type: Array, required: true },
  search: { type: String, required: true },
})
const emit = defineEmits([
  'update:collapsedRowGroups',
  'update:selectedGroupRows',
  'update:selectedItems',
  'on-ungroup',
])

const {
  lodash: { isEmpty, isEqual, unionWith, differenceWith },
  immutableUpdate,
} = useHelpers()

/** A workaround to get maximum width of row group header
 * 16 is the total width of padding and border of table
 * 24 is the width of toggle button
 * 28 is the width of ungroup checkbox
 */
const maxVisWidth = computed(() => props.boundingWidth - 16 - 24 - 28)
const highlighterData = computed(() => {
  return {
    keyword:
      props.filterByColIndexes.includes(props.row.groupByColIdx) || !props.filterByColIndexes.length
        ? props.search
        : '',
    txt: props.row.value,
  }
})
const groupedItems = computed(() => {
  const targetIdx = props.tableData.findIndex((ele) => isEqual(ele, props.row))
  let items = []
  let i = targetIdx + 1
  while (i !== -1) {
    if (Array.isArray(props.tableData[i])) {
      items.push(props.tableData[i])
      i++
    } else i = -1
  }
  return items
})
const selectedGroupIdx = computed(() =>
  props.selectedGroupRows.findIndex((ele) => isEqual(ele, props.row))
)
const isAllSelected = computed(() =>
  isEmpty(differenceWith(groupedItems.value, props.selectedItems, isEqual))
)
const indeterminate = computed(
  () =>
    !isAllSelected.value &&
    groupedItems.value.some((groupedItem) =>
      props.selectedItems.some((selectedItem) => isEqual(selectedItem, groupedItem))
    )
)

function toggleRowGroup() {
  const targetIdx = props.collapsedRowGroups.findIndex((r) => isEqual(props.row, r))
  emit(
    'update:collapsedRowGroups',
    targetIdx >= 0
      ? immutableUpdate(props.collapsedRowGroups, { $splice: [[targetIdx, 1]] })
      : immutableUpdate(props.collapsedRowGroups, { $push: [props.row] })
  )
}

function handleUngroup() {
  emit('update:collapsedRowGroups', [])
  emit('on-ungroup')
}

/**
 * @param {Boolean} v - is row group selected
 */
function toggleGroupItemSelection(v) {
  emit(
    'update:selectedItems',
    v
      ? unionWith(props.selectedItems, groupedItems.value, isEqual)
      : differenceWith(props.selectedItems, groupedItems.value, isEqual)
  )
}

/**
 * @param {Boolean} v - is row group selected
 */
function toggleGroupSelection(v) {
  emit(
    'update:selectedGroupRows',
    v
      ? immutableUpdate(props.selectedGroupRows, { $push: [props.row] })
      : immutableUpdate(props.selectedGroupRows, { $splice: [[selectedGroupIdx.value, 1]] })
  )
  toggleGroupItemSelection(v)
}
</script>

<template>
  <div class="tr tr--group" :style="{ lineHeight }">
    <div
      class="d-flex align-center td pl-1 pr-3 mxs-helper-class border-right-table-border"
      :style="{
        height: lineHeight,
        minWidth: `${maxWidth}px`,
        maxWidth: `${maxWidth}px`,
      }"
    >
      <VBtn variant="text" density="compact" :height="24" :width="24" icon @click="toggleRowGroup">
        <VIcon
          :class="[isCollapsed ? 'rotate-right' : 'rotate-down']"
          color="navigation"
          icon="$mdiChevronDown"
        />
      </VBtn>
      <VCheckboxBtn
        v-if="showSelect"
        :modelValue="isAllSelected"
        :indeterminate="indeterminate"
        inline
        density="compact"
        :style="{ marginLeft: '-2px' }"
        @update:modelValue="toggleGroupSelection"
      />
      <div
        class="tr--group__content d-inline-flex align-center"
        :style="{ maxWidth: `${maxVisWidth}px` }"
      >
        <GblTooltipActivator
          class="font-weight-bold"
          :data="{ txt: `${row.groupBy}` }"
          :maxWidth="maxVisWidth * 0.15"
          activateOnTruncation
        />
        <span class="d-inline-block val-separator mr-4">:</span>
        <GblTooltipActivator
          v-mxs-highlighter="highlighterData"
          :data="{ txt: `${row.value}` }"
          :maxWidth="maxVisWidth * 0.85"
          activateOnTruncation
        />
      </div>
      <TooltipBtn
        class="ml-2"
        variant="text"
        density="compact"
        icon
        :height="24"
        :width="24"
        color="primary"
        @click="handleUngroup"
      >
        <template #btn-content>
          <VIcon size="10" icon="mxs:close" />
        </template>
        {{ $t('ungroup') }}
      </TooltipBtn>
    </div>
  </div>
</template>
