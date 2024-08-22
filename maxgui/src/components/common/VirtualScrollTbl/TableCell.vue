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
const props = defineProps({
  slotName: { type: String, required: true },
  slotData: { type: Object, required: true },
  isDragging: { type: Boolean, required: true },
  search: { type: String, required: true },
  filterByColIndexes: { type: Array, required: true },
  mousedownHandler: { type: Function, required: true },
  contextmenuHandler: { type: Function, required: true },
})

const typy = useTypy()

const isCellDraggable = computed(() => typy(props.slotData, 'header.draggable').safeBoolean)
const highlighterData = computed(() => ({
  keyword:
    props.filterByColIndexes.includes(props.slotData.colIdx) || !props.filterByColIndexes.length
      ? props.search
      : '',
  txt: props.slotData.cell,
}))
const cellProps = computed(
  () => typy(props.slotData, 'header.cellProps').safeObject || { class: 'px-3' }
)
const useCellSlot = computed(() => props.slotData.header.useCellSlot)

function mousedown(e) {
  if (isCellDraggable.value) props.mousedownHandler(e)
}

function contextmenu(e, activatorID) {
  e.preventDefault()
  props.contextmenuHandler({
    e,
    row: props.slotData.rowData,
    cell: props.slotData.cell,
    activatorID: activatorID || typy(e, 'target.id').safeString,
  })
}
</script>
<!-- Use useCellSlot as key to rerender the cell
to make sure the slot is rendered with accurate slot content,
otherwise, it renders the old content created by the highlighter directive
-->
<!--TODO: Using a dialog component would be better in terms of UX for
showing truncated text -->
<template>
  <GblTooltipActivator
    tag="div"
    class="td text-navigation"
    :class="{ 'cursor--grab user-select--none': isCellDraggable }"
    :data="{ txt: String(slotData.cell), interactive: true }"
    :key="useCellSlot"
    activateOnTruncation
    :disabled="useCellSlot || isDragging"
    v-mxs-highlighter="useCellSlot ? null : highlighterData"
    v-on="useCellSlot ? {} : { contextmenu, mousedown }"
    v-bind="cellProps"
  >
    <template #default="{ value, activatorID }">
      <slot
        :name="slotName"
        :on="{ contextmenu: (e) => contextmenu(e, activatorID), mousedown }"
        :highlighterData="highlighterData"
        :data="slotData"
        :activatorID="activatorID"
      >
        {{ value }}
      </slot>
    </template>
  </GblTooltipActivator>
</template>
