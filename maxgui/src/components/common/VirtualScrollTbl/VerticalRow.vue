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
import TableCell from '@/components/common/VirtualScrollTbl/TableCell.vue'

/**
 * In vertical-row mode, there are two headers which are `COLUMN` and `VALUE`.
 * `COLUMN`: render sql columns
 * `VALUE`: render sql column value
 * In this mode,
 * rowIdx: indicates to the row index
 * colIdx: indicates the column index in row (props)
 * 0: indicates the `COLUMN` index in the headers
 * 1: indicates the `VALUE` index in the headers
 */

const props = defineProps({
  row: { type: Array, required: true },
  rowIdx: { type: Number, required: true },
  tableHeaders: {
    type: Array,
    validator: (arr) => {
      if (!arr.length) return true
      else return arr.filter((item) => 'text' in item).length === arr.length
    },
    required: true,
  },
  lineHeight: { type: String, required: true },
  colWidths: { type: Array, required: true },
  cellContentWidths: { type: Array, required: true },
  isDragging: { type: Boolean, default: true },
  search: { type: String, required: true },
  filterByColIndexes: { type: Array, required: true },
  cellProps: { type: Object, required: true },
})

const { handleAddPxUnit } = useHelpers()
const typy = useTypy()

const baseColStyle = computed(() => ({ lineHeight: props.lineHeight, height: props.lineHeight }))
const headerColStyle = computed(() => ({
  ...baseColStyle.value,
  minWidth: handleAddPxUnit(props.colWidths[0]),
}))
const valueColStyle = computed(() => ({
  ...baseColStyle.value,
  minWidth: handleAddPxUnit(props.colWidths[1]),
}))
const headerContentWidth = computed(() => typy(props.cellContentWidths, '[0]').safeNumber)

const valueContentWidth = computed(() => typy(props.cellContentWidths, '[1]').safeNumber)
</script>

<template>
  <div class="tr-vertical-group d-flex flex-column">
    <template v-for="(h, colIdx) in tableHeaders">
      <div
        v-if="!h.hidden"
        :key="`${h.text}_${colIdx}`"
        class="tr align-center"
        :style="{ height: lineHeight }"
      >
        <TableCell
          :style="headerColStyle"
          :slotName="`header-${h.text}`"
          :slotData="{
            rowData: row,
            rowIdx,
            cell: h.text,
            colIdx,
            header: h,
            maxWidth: headerContentWidth,
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
        <TableCell
          :style="valueColStyle"
          :slotName="h.text"
          :slotData="{
            rowData: row,
            rowIdx,
            cell: row[colIdx],
            colIdx,
            header: h,
            maxWidth: valueContentWidth,
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
      </div>
    </template>
  </div>
</template>

<style lang="scss" scoped>
.tr-vertical-group {
  .tr {
    .td {
      border-bottom: none !important;
    }
  }
  .tr:last-of-type {
    .td {
      border-bottom: thin solid colors.$table-border !important;
    }
  }
}
</style>
