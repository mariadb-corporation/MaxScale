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
import TableCell from '@/components/common/VirtualScrollTbl/TableCell.vue'

const props = defineProps({
  row: { type: [Array, Object], required: true },
  rowIdx: { type: Number, required: true },
  selectedItems: { type: Array, required: true },
  areHeadersHidden: { type: Boolean, required: true },
  tableHeaders: { type: Array, required: true },
  lineHeight: { type: String, required: true },
  showSelect: { type: Boolean, required: true },
  checkboxColWidth: { type: Number, required: true },
  activeRow: { type: [Array, Object], required: true },
  colWidths: { type: Array, required: true },
  cellContentWidths: { type: Array, required: true },
  isDragging: { type: Boolean, default: true },
  search: { type: String, required: true },
  singleSelect: { type: Boolean, required: true },
  filterByColIndexes: { type: Array, required: true },
  cellProps: { type: Object, required: true },
})
const emit = defineEmits(['update:selectedItems'])

const {
  lodash: { isEqual },
  immutableUpdate,
} = useHelpers()

const selectedRowIdx = computed(() =>
  props.selectedItems.findIndex((ele) => isEqual(ele, props.row))
)
const isRowSelected = computed(() => selectedRowIdx.value >= 0)

function toggleSelect(v) {
  if (v)
    emit(
      'update:selectedItems',
      props.singleSelect
        ? [props.row]
        : immutableUpdate(props.selectedItems, { $push: [props.row] })
    )
  else
    emit(
      'update:selectedItems',
      immutableUpdate(props.selectedItems, { $splice: [[selectedRowIdx.value, 1]] })
    )
}
</script>

<template>
  <div
    class="tr"
    :class="{
      'tr--selected': isRowSelected,
      'tr--active': $helpers.lodash.isEqual(activeRow, row),
    }"
    :style="{ lineHeight }"
  >
    <div
      v-if="!areHeadersHidden && showSelect"
      class="td d-flex align-center justify-center"
      :style="{
        height: lineHeight,
        maxWidth: `${checkboxColWidth}px`,
        minWidth: `${checkboxColWidth}px`,
      }"
    >
      <VCheckbox
        :modelValue="isRowSelected"
        primary
        hide-details
        density="compact"
        @update:modelValue="toggleSelect"
        @click.stop
      />
    </div>
    <template v-for="(h, colIdx) in tableHeaders">
      <TableCell
        v-if="!h.hidden"
        :key="`${h.text}_${colWidths[colIdx]}_${colIdx}`"
        :style="{ height: lineHeight, minWidth: $helpers.handleAddPxUnit(colWidths[colIdx]) }"
        :slotName="h.text"
        :slotData="{
          rowData: row,
          rowIdx,
          cell: row[colIdx],
          colIdx,
          header: h,
          maxWidth: $typy(cellContentWidths[colIdx]).safeNumber,
        }"
        :isDragging="isDragging"
        :search="search"
        :filterByColIndexes="filterByColIndexes"
        v-bind="cellProps"
      >
        <template v-for="(_, name) in $slots" #[name]="slotData">
          <slot :name="name" v-bind="slotData" />
        </template>
      </TableCell>
    </template>
  </div>
</template>
