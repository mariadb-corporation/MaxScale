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

import TableHeader from '@/components/common/VirtualScrollTbl/TableHeader.vue'
import VerticalRow from '@/components/common/VirtualScrollTbl/VerticalRow.vue'
import HorizRow from '@/components/common/VirtualScrollTbl/HorizRow.vue'
import RowGroup from '@/components/common/VirtualScrollTbl/RowGroup.vue'

const props = defineProps({
  headers: {
    type: Array,
    validator: (arr) => {
      if (!arr.length) return true
      else return arr.filter((item) => 'text' in item).length === arr.length
    },
    required: true,
  },
  data: { type: Array, required: true },
  maxHeight: { type: Number, required: true },
  itemHeight: { type: Number, required: true },
  boundingWidth: { type: Number, required: true },
  isVertTable: { type: Boolean, default: false },
  showSelect: { type: Boolean, default: false },
  singleSelect: { type: Boolean, default: false },
  groupByColIdx: { type: Number, default: -1 },
  // row being highlighted. e.g. opening ctx menu of a row
  activeRow: { type: Array, default: () => [] },
  search: { type: String, default: '' }, // Text input used to highlight cell
  filterByColIndexes: { type: Array, default: () => [] },
  noDataText: { type: String, default: '' },
  selectedItems: { type: Array, default: () => [] },
  showRowCount: { type: Boolean, default: true },
  contextmenuHandler: { type: Function, default: () => null },
})
const emit = defineEmits([
  'update:selectedItems',
  'update:groupByColIdx',
  'current-rows',
  'row-click',
  'on-dragging', // v:object. Event emitted from useDragAndDrop
  'on-dragend', // v:object. Event emitted from useDragAndDrop
])

const {
  getScrollbarWidth,
  dateFormat,
  lodash: { isEqual, cloneDeep },
  ciStrIncludes,
} = useHelpers()
const typy = useTypy()
const scrollBarThickness = getScrollbarWidth()
const { isDragging, dragTarget } = useDragAndDrop((event, data) => emit(event, data))

const scrollerRef = ref(null)
const lastScrollTop = ref(0)
const headerWidths = ref([])
const headerStyle = ref({})
const isResizing = ref(false)
const sortOptions = ref({ sortByColIdx: -1, sortDesc: false })
const collapsedRowGroups = ref([])
const selectedGroupRows = ref([])

const tableData = computed(() => {
  let data = cloneDeep(props.data)
  if (sortOptions.value.sortByColIdx >= 0) sortData(data)
  if (props.groupByColIdx !== -1 && !props.isVertTable) data = groupData(data)
  return data
})
const tableHeaders = computed(() =>
  props.groupByColIdx === -1
    ? props.headers
    : props.headers.map((h, i) => (props.groupByColIdx === i ? { ...h, hidden: true } : h))
)
const visHeaders = computed(() => tableHeaders.value.filter((h) => !h.hidden))
const rows = computed(() => filterData(tableData.value))
const rowHeight = computed(() =>
  props.isVertTable ? props.itemHeight * visHeaders.value.length : props.itemHeight
)
const rowsHeight = computed(() => rows.value.length * rowHeight.value + scrollBarThickness)
const maxTbodyHeight = computed(
  () => props.maxHeight - 30 // header fixed height is 30px
)
const isYOverflowed = computed(() => rowsHeight.value > maxTbodyHeight.value)

const maxBoundingWidth = computed(
  () =>
    // minus scrollbar thickness if body is vertically overflow
    props.boundingWidth - (isYOverflowed.value ? scrollBarThickness + 1 : 0)
)

const lineHeight = computed(() => `${props.itemHeight}px`)
const maxRowGroupWidth = computed(() => {
  let width = headerWidths.value.reduce((acc, v, idx) => {
    if (idx !== props.groupByColIdx) acc += typy(v).safeNumber
    return acc
  }, 0)
  if (props.showSelect) width += checkboxColWidth.value
  return width
})
const checkboxColWidth = computed(() => (props.groupByColIdx >= 0 ? 82 : 50))
const dataCount = computed(() => props.data.length)
// indicates the number of filtered rows excluding row group objects
const rowCount = computed(() => rows.value.filter((row) => !isGroupedRow(row)).length)

const isAllSelected = computed(
  () => props.selectedItems.length > 0 && props.selectedItems.length === dataCount.value
)
const indeterminate = computed(() => {
  if (!props.selectedItems.length) return false
  return !isAllSelected.value && props.selectedItems.length < dataCount.value
})
const areHeadersHidden = computed(() => visHeaders.value.length === 0)
const cellContentWidths = computed(
  () => headerWidths.value.map((w) => w - 24) // minus padding. i.e px-3
)

watch(
  () => props.isVertTable,
  (v) => {
    // clear selectedItems
    if (v) emit('update:selectedItems', [])
  }
)

onActivated(() => setLastScrollPos())

function setLastScrollPos() {
  if (scrollerRef.value) {
    scrollerRef.value.scrollTop = 1
    scrollerRef.value.scrollTop = lastScrollTop.value
  }
}

function scrolling() {
  //make table header to "scrollX" as well
  headerStyle.value = {
    ...headerStyle.value,
    position: 'relative',
    left: `-${scrollerRef.value.scrollLeft}px`,
  }
  lastScrollTop.value = scrollerRef.value.scrollTop
}

//SORT FEAT
/**
 * @param {Array} data - 2d array to be sorted
 */
function sortData(data) {
  const { sortDesc, sortByColIdx } = sortOptions.value
  data.sort((a, b) => {
    if (sortDesc) return b[sortByColIdx] < a[sortByColIdx] ? -1 : 1
    else return a[sortByColIdx] < b[sortByColIdx] ? -1 : 1
  })
}

// GROUP feat
/** This groups 2d array with same value at provided index to a Map
 * @param {Array} payload.data - 2d array to be grouped into a Map
 * @param {Number} payload.idx - col index of the inner array
 * @returns {Map} - returns map with value as key and value is a matrix (2d array)
 */
function groupValues({ data, idx, header }) {
  let map = new Map()
  data.forEach((row) => {
    let key = row[idx]
    if (header.dateFormatType) key = formatDate(row[idx], header.dateFormatType)
    if (header.valuePath) key = row[idx][header.valuePath]
    let matrix = map.get(key) || [] // assign an empty arr if not found
    matrix.push(row)
    map.set(key, matrix)
  })
  return map
}

function groupData(data) {
  const header = props.headers[props.groupByColIdx]
  const rowMap = groupValues({ data, idx: props.groupByColIdx, header })
  let groupRows = []
  for (const [key, value] of rowMap) {
    groupRows.push({
      groupBy: header.text,
      groupByColIdx: props.groupByColIdx,
      value: key,
      groupLength: value.length,
    })
    groupRows = [...groupRows, ...value]
  }
  return groupRows
}

/**
 * @param {Object|Array} row - row to check
 * @returns {Boolean} - return whether row is a grouped row or not
 */
function isGroupedRow(row) {
  return typy(row).isObject
}

/**
 *  If provided row is found in collapsedRowGroups data, it's collapsed
 * @param {Object} row - row group object
 * @returns {Boolean} - return true if it is collapsed
 */
function isRowGroupCollapsed(row) {
  const targetIdx = collapsedRowGroups.value.findIndex((r) => isEqual(row, r))
  return targetIdx === -1 ? false : true
}

function ungroup() {
  emit('update:groupByColIdx', -1)
}

// SELECT feat
/**
 * @param {Boolean} v - is row selected
 */
function handleSelectAll(v) {
  // don't select group row
  if (v) {
    emit(
      'update:selectedItems',
      tableData.value.filter((row) => Array.isArray(row))
    )
    selectedGroupRows.value = tableData.value.filter((row) => !Array.isArray(row))
  } else {
    emit('update:selectedItems', [])
    selectedGroupRows.value = []
  }
}

function onCellDragStart(e) {
  e.preventDefault()
  // Assign value to data in dragAndDrop mixin
  isDragging.value = true
  dragTarget.value = e.target
}

/**
 * Filter row by `search` keyword and `filterByColIndexes`
 * @param {Array.<Array>} row
 * @returns {boolean}
 */
function rowFilter(row) {
  if (!props.search) return true
  return row.some((cell, colIdx) => {
    const header = typy(props.headers[colIdx]).safeObjectOrEmpty
    return (
      (props.filterByColIndexes.includes(colIdx) || !props.filterByColIndexes.length) &&
      filter({ header, value: cell })
    )
  })
}

function filter({ header, value }) {
  let str = value
  if (header.dateFormatType) str = formatDate(value, header.dateFormatType)
  if (header.valuePath) str = value[header.valuePath]
  return ciStrIncludes(`${str}`, props.search)
}

/**
 * Filter for row group
 * @param {Array.<Array>} param.data
 * @param {object} param.rowGroup
 * @param {number} param.rowIdx
 * @returns {boolean}
 */
function rowGroupFilter({ data, rowGroup, rowIdx }) {
  return Array(rowGroup.groupLength)
    .fill()
    .map((_, n) => data[n + rowIdx + 1])
    .some((row) => rowFilter(row))
}

function filterData(data) {
  let collapsedRowIndices = []
  return data.filter((row, rowIdx) => {
    const isGrouped = isGroupedRow(row)
    if (isGrouped) {
      // get indexes of collapsed rows
      if (isRowGroupCollapsed(row))
        collapsedRowIndices = [
          ...collapsedRowIndices,
          ...Array(row.groupLength)
            .fill()
            .map((_, n) => n + rowIdx + 1),
        ]
    }
    if (collapsedRowIndices.includes(rowIdx)) return false
    if (isGrouped) return rowGroupFilter({ data, rowGroup: row, rowIdx })
    return rowFilter(row)
  })
}

function formatDate(value, formatType) {
  return dateFormat({ value, formatType })
}
defineExpose({ scrollBarThickness })
</script>

<template>
  <div
    class="virtual-table w-100"
    :class="{ 'no-userSelect': isResizing }"
    :style="{ cursor: isResizing ? 'col-resize' : '' }"
  >
    <TableHeader
      v-model:sortOptions="sortOptions"
      :isVertTable="isVertTable"
      :items="tableHeaders"
      :boundingWidth="maxBoundingWidth"
      :headerStyle="headerStyle"
      :rowCount="rowCount"
      :showSelect="showSelect"
      :checkboxColWidth="checkboxColWidth"
      :isAllSelected="isAllSelected"
      :indeterminate="indeterminate"
      :areHeadersHidden="areHeadersHidden"
      :scrollBarThickness="scrollBarThickness"
      :singleSelect="singleSelect"
      :showRowCount="showRowCount"
      @header-widths="headerWidths = $event"
      @is-resizing="isResizing = $event"
      @toggle-select-all="handleSelectAll"
    >
      <template v-for="(_, name) in $slots" #[name]="slotData">
        <slot :name="name" v-bind="slotData" />
      </template>
    </TableHeader>
    <div
      v-if="dataCount && !areHeadersHidden"
      ref="scrollerRef"
      class="tbody overflow-auto relative"
      :style="{
        maxHeight: `${maxTbodyHeight}px`,
        height: `${(isYOverflowed ? maxTbodyHeight : rowsHeight) + scrollBarThickness}px`,
      }"
      @scroll="scrolling"
    >
      <VVirtualScroll :items="rows" :item-height="rowHeight" renderless>
        <template #default="{ item: row, index: rowIdx, itemRef }">
          <VerticalRow
            v-if="isVertTable"
            :ref="itemRef"
            :row="row"
            :rowIdx="rowIdx"
            :tableHeaders="tableHeaders"
            :lineHeight="lineHeight"
            :colWidths="headerWidths"
            :cellContentWidths="cellContentWidths"
            :isDragging="isDragging"
            :search="search"
            :filterByColIndexes="filterByColIndexes"
            :cellProps="{ mousedownHandler: onCellDragStart, contextmenuHandler }"
            @click="emit('row-click', row)"
            v-bind="itemRef"
          >
            <template v-for="(_, name) in $slots" #[name]="slotData">
              <slot :name="name" v-bind="slotData" />
            </template>
          </VerticalRow>
          <RowGroup
            v-else-if="isGroupedRow(row) && !areHeadersHidden"
            :ref="itemRef"
            v-model:collapsedRowGroups="collapsedRowGroups"
            v-model:selectedGroupRows="selectedGroupRows"
            :selectedItems="selectedItems"
            :row="row"
            :tableData="tableData"
            :isCollapsed="isRowGroupCollapsed(row)"
            :boundingWidth="maxBoundingWidth"
            :lineHeight="lineHeight"
            :showSelect="showSelect"
            :maxWidth="maxRowGroupWidth"
            :search="search"
            :filterByColIndexes="filterByColIndexes"
            @on-ungroup="ungroup"
            @click="emit('row-click', row)"
            @update:selectedItems="emit('update:selectedItems', $event)"
          />
          <HorizRow
            v-else
            :ref="itemRef"
            :row="row"
            :rowIdx="rowIdx"
            :selectedItems="selectedItems"
            :areHeadersHidden="areHeadersHidden"
            :tableHeaders="tableHeaders"
            :lineHeight="lineHeight"
            :showSelect="showSelect"
            :checkboxColWidth="checkboxColWidth"
            :activeRow="activeRow"
            :colWidths="headerWidths"
            :cellContentWidths="cellContentWidths"
            :isDragging="isDragging"
            :search="search"
            :filterByColIndexes="filterByColIndexes"
            :singleSelect="singleSelect"
            @click="emit('row-click', row)"
            @update:selectedItems="emit('update:selectedItems', $event)"
            :cellProps="{ mousedownHandler: onCellDragStart, contextmenuHandler }"
          >
            <template v-for="(_, name) in $slots" #[name]="slotData">
              <slot :name="name" v-bind="slotData" />
            </template>
          </HorizRow>
        </template>
      </VVirtualScroll>
    </div>
    <div v-else class="tr" :style="{ lineHeight, height: `${maxTbodyHeight}px` }">
      <div class="td px-3 no-data-text d-flex justify-center flex-grow-1">
        {{ noDataText ? noDataText : $t('noDataText') }}
      </div>
    </div>
    <div v-if="isResizing" class="dragging-mask" />
  </div>
</template>

<style lang="scss" scoped>
.virtual-table {
  :deep(.tbody) {
    .tr {
      display: flex;
      cursor: pointer;
      .td {
        font-size: 0.875rem;
        color: colors.$navigation;
        border-bottom: thin solid colors.$table-border;
        border-right: thin solid colors.$table-border;
        background: white;
        &:first-of-type {
          border-left: thin solid colors.$table-border;
        }
      }
      &:hover {
        .td {
          background: colors.$tr-hovered-color;
        }
      }
      &:active,
      &--active,
      &--selected {
        .td {
          background: colors.$selected-tr-color !important;
        }
      }
    }
  }
  .no-data-text {
    font-size: 0.875rem;
  }
}
</style>
