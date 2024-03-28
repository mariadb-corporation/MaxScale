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
  /*
   * items: {
   * width?: string | number, default width when header is rendered
   * maxWidth?: string | number, if maxWidth is declared, it ignores width and use it as default width
   * minWidth?: string | number, allow resizing column to no smaller than provided value
   * resizable?: boolean, true by default
   * capitalize?: boolean, capitalize first letter of the header
   * uppercase?: boolean, uppercase all letters of the header
   * groupable?: boolean
   * hidden?: boolean, hidden the column
   * draggable?: boolean, emits on-cell-dragging and on-cell-dragend events when dragging the content of the cell
   * sortable?: boolean, if false, column won't be sortable
   * required?: boolean, if true, `label-required` class will be added to the header
   * useCellSlot?: boolean, if true, v-mxs-highlighter directive and mouse events won't be added
   * // attributes to be used with filtering
   * valuePath?: string. When value of the cell is an object.
   * dateFormatType?: string. date-fns format, E, dd MMM yyyy.
   *}
   */
  items: { type: Array, required: true },
  boundingWidth: { type: Number, required: true },
  headerStyle: { type: Object, required: true },
  isVertTable: { type: Boolean, default: false },
  rowCount: { type: Number, required: true },
  showSelect: { type: Boolean, required: true },
  singleSelect: { type: Boolean, required: true },
  checkboxColWidth: { type: Number, required: true },
  isAllSelected: { type: Boolean, required: true },
  indeterminate: { type: Boolean, required: true },
  areHeadersHidden: { type: Boolean, required: true },
  scrollBarThickness: { type: Number, required: true },
  showRowCount: { type: Boolean, required: true },
  sortOptions: { type: Object, required: true },
})
const emit = defineEmits([
  'update:sortOptions',
  'is-resizing',
  'header-widths',
  'toggle-select-all',
])

const {
  lodash: { isEqual },
  uuidv1,
  handleAddPxUnit,
} = useHelpers()
const typy = useTypy()

const headerRef = ref({})
const headerWidths = ref([])
const isResizing = ref(false)
const resizingData = ref(null)
const headerIds = ref([])

const headers = computed(() =>
  props.isVertTable
    ? [
        { text: 'COLUMN', width: '20%' },
        { text: 'VALUE', width: '80%' },
      ]
    : props.items
)
const visHeaders = computed(() => headers.value.filter((h) => !h.hidden))
const lastVisHeader = computed(() => visHeaders.value.at(-1))
const sortOpts = computed({
  get: () => props.sortOptions,
  set: (v) => emit('update:sortOptions', v),
})
const enableSorting = computed(() => props.rowCount <= 10000 && !props.isVertTable)
//threshold, user cannot resize header smaller than 67px
const headerMinWidths = computed(() => headers.value.map((h) => h.minWidth || 67))

watch(isResizing, (v) => emit('is-resizing', v))
watch(
  headers,
  (v, oV) => {
    if (!isEqual(v, oV)) {
      headerIds.value = Array.from({ length: v.length }, () => `header-${uuidv1()}`)
    }
  },
  { deep: true, immediate: true }
)
watch(
  headerIds,
  (v, oV) => {
    if (!isEqual(v, oV)) {
      resetHeaderWidths()
      nextTick(() => computeHeaderWidths())
    }
  },
  { immediate: true, deep: true }
)
watch(
  () => props.boundingWidth,
  () => {
    resetHeaderWidths()
    nextTick(() => computeHeaderWidths())
  }
)
onBeforeMount(() => {
  window.addEventListener('mousemove', resizerMouseMove)
  window.addEventListener('mouseup', resizerMouseUp)
})
onBeforeUnmount(() => {
  window.removeEventListener('mousemove', resizerMouseMove)
  window.removeEventListener('mouseup', resizerMouseUp)
})

function isSortable(header) {
  return enableSorting.value && header.sortable !== false
}

function isResizerDisabled(header) {
  return header.text === '#' || header.resizable === false
}

function headerTxtMaxWidth({ header, index }) {
  const w = typy(headerWidths.value[index]).safeNumber - (isSortable(header) ? 22 : 0) - 24 // minus 24px padding
  return w > 0 ? w : 1
}

function resetHeaderWidths() {
  headerWidths.value = headers.value.map((header) =>
    header.hidden ? 0 : header.maxWidth || header.width || 'unset'
  )
}

function getHeaderWidth(headerIdx) {
  const headerEle = typy(headerRef.value, `[${headerIdx}]`).safeObject
  if (headerEle) {
    const { width } = headerEle.getBoundingClientRect()
    return width
  }
  return 0
}

function computeHeaderWidths() {
  headerWidths.value = headers.value.map((header, index) => {
    const minWidth = headerMinWidths.value[index]
    const width = getHeaderWidth(index)
    return header.hidden ? 0 : width < minWidth ? minWidth : width
  })
  emit('header-widths', headerWidths.value)
}

function getLastNode() {
  const index = headers.value.indexOf(lastVisHeader.value)
  return {
    node: typy(headerRef.value, `[${index}]`).safeObject,
    lastNodeMinWidth: headerMinWidths.value[index],
  }
}

function resizerMouseDown(e, index) {
  resizingData.value = {
    currPageX: e.pageX,
    targetNode: e.target.parentElement,
    targetNodeWidth: e.target.parentElement.offsetWidth,
    targetHeaderMinWidth: headerMinWidths.value[index],
  }
  isResizing.value = true
}

function resizerMouseMove(e) {
  if (isResizing.value) {
    const { currPageX, targetNode, targetNodeWidth, targetHeaderMinWidth } = resizingData.value
    const diffX = e.pageX - currPageX
    const targetNewWidth = targetNodeWidth + diffX
    if (targetNewWidth >= targetHeaderMinWidth) {
      targetNode.style.maxWidth = handleAddPxUnit(targetNewWidth)
      targetNode.style.minWidth = handleAddPxUnit(targetNewWidth)
      const { node: lastNode, lastNodeMinWidth } = getLastNode()
      if (lastNode) {
        // Let the last header node to auto grow its width
        lastNode.style.maxWidth = 'unset'
        lastNode.style.minWidth = 'unset'
        // use the default min width if the actual width is smaller than it
        const { width: currentLastNodeWidth } = lastNode.getBoundingClientRect()
        if (currentLastNodeWidth < lastNodeMinWidth) {
          lastNode.style.maxWidth = handleAddPxUnit(lastNodeMinWidth)
          lastNode.style.minWidth = handleAddPxUnit(lastNodeMinWidth)
        }
      }
    }
  }
}

function resizerMouseUp() {
  if (isResizing.value) {
    isResizing.value = false
    computeHeaderWidths()
    resizingData.value = null
  }
}

/**
 * Update sort options in three states: initial, asc or desc
 * @param {number} - index
 */
function updateSortOpts(index) {
  if (sortOpts.value.sortByColIdx === index) {
    if (sortOpts.value.sortDesc) sortOpts.value.sortByColIdx = -1
    sortOpts.value.sortDesc = !sortOpts.value.sortDesc
  } else {
    sortOpts.value.sortByColIdx = index
    sortOpts.value.sortDesc = false
  }
}
</script>

<template>
  <div class="virtual-table__header d-flex relative">
    <div class="thead d-flex" :style="{ width: `${boundingWidth}px` }">
      <div
        v-if="!areHeadersHidden && showSelect && !isVertTable"
        class="th d-flex justify-center align-center"
        :style="{
          ...headerStyle,
          maxWidth: `${checkboxColWidth}px`,
          minWidth: `${checkboxColWidth}px`,
        }"
      >
        <template v-if="!singleSelect">
          <VCheckbox
            :modelValue="isAllSelected"
            :indeterminate="indeterminate"
            class="ma-0 pa-0"
            primary
            hide-details
            density="compact"
            @update:modelValue="(val) => emit('toggle-select-all', val)"
          />
          <div class="header__resizer no-pointerEvent d-inline-block fill-height" />
        </template>
      </div>
      <template v-for="(header, index) in headers">
        <div
          v-if="!header.hidden"
          :id="headerIds[index]"
          :key="headerIds[index]"
          :ref="(el) => (headerRef[index] = el)"
          :style="{
            ...headerStyle,
            maxWidth: $helpers.handleAddPxUnit(headerWidths[index]),
            minWidth: $helpers.handleAddPxUnit(headerWidths[index]),
          }"
          class="th d-flex align-center px-3"
          :class="{
            pointer: enableSorting && header.sortable !== false,
            [`sort--active ${sortOpts.sortDesc ? 'desc' : 'asc'}`]: sortOpts.sortByColIdx === index,
            'text-capitalize': header.capitalize,
            'text-uppercase': header.uppercase,
            'th--resizable': !isResizerDisabled(header),
            'label-required': header.required,
          }"
          @click="isSortable(header) ? updateSortOpts(index) : null"
        >
          <template v-if="index === 0 && header.text === '#'">
            {{ header.text }}
            <span v-if="showRowCount" class="ml-1 text-grayed-out"> ({{ rowCount }}) </span>
          </template>
          <slot
            v-else
            :name="`header-${header.text}`"
            :data="{
              header,
              // maxWidth: minus padding and sort-icon
              maxWidth: headerTxtMaxWidth({ header, index }),
              colIdx: index,
              activatorID: headerIds[index],
            }"
          >
            <span class="text-truncate">{{ header.text }} </span>
          </slot>
          <VIcon v-if="isSortable(header)" size="14" class="sort-icon ml-2" icon="mxs:arrowDown" />
          <span
            v-if="index < headers.length - 1"
            class="header__resizer d-inline-block fill-height"
            :data-test="`${header.text}-resizer-ele`"
            v-on="isResizerDisabled(header) ? {} : { mousedown: (e) => resizerMouseDown(e, index) }"
          />
        </div>
      </template>
    </div>
    <div :style="{ minWidth: `${scrollBarThickness}px` }" class="d-inline-block fixed-padding" />
  </div>
</template>

<style lang="scss" scoped>
.virtual-table__header {
  height: 30px;
  overflow: hidden;
  .thead {
    .th {
      position: relative;
      z-index: 1;
      flex: 1;
      font-weight: bold;
      font-size: 0.75rem;
      color: colors.$small-text;
      background-color: colors.$table-border;
      border-bottom: none;
      user-select: none;
      height: 30px;
      &:first-child {
        border-radius: 5px 0 0 0;
      }
      &:last-child {
        padding-right: 0px !important;
      }
      .sort-icon {
        transform: none;
        visibility: hidden;
      }
      &.sort--active {
        color: black;
      }
      &.sort--active .sort-icon {
        color: inherit;
        visibility: visible;
      }
      &.desc {
        .sort-icon {
          transform: rotate(-180deg);
        }
      }
      &:hover {
        .sort-icon {
          visibility: visible;
        }
      }
      .header__resizer {
        position: absolute;
        right: 0px;
        width: 11px;
        border-right: 1px solid white;
        // disabled by default
        cursor: initial;
        &--hovered,
        &:hover {
          border-right: 1px solid white;
        }
      }
      // Enable when have th--resizable class
      &--resizable {
        .header__resizer {
          cursor: ew-resize;
          border-right: 1px solid white;
          &--hovered,
          &:hover {
            border-right: 3px solid white;
          }
        }
      }
      &.label-required::after {
        left: 4px;
      }
    }
  }

  .fixed-padding {
    z-index: 2;
    background-color: colors.$table-border;
    height: 30px;
    position: absolute;
    border-radius: 0 5px 0 0;
    right: 0;
  }
}
</style>
