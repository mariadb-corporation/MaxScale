<template>
    <!-- Use useCellSlot as key to rerender the cell
    to make sure the slot is rendered with accurate slot content,
    otherwise, it renders the old content created by the highlighter directive
    -->
    <div
        :id="slotData.activatorID"
        ref="cell"
        :key="useCellSlot"
        v-mxs-highlighter="useCellSlot ? null : highlighterData"
        class="td px-3 d-inline-block text-truncate"
        :class="{ [draggableClass]: isCellDraggable }"
        v-on="useCellSlot ? null : { mousedown, mouseenter, contextmenu }"
    >
        <slot
            :name="slotName"
            :on="{ mousedown, mouseenter, contextmenu }"
            :highlighterData="highlighterData"
            :data="slotData"
        >
            {{ slotData.cell }}
        </slot>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import { mapMutations } from 'vuex'
export default {
    name: 'table-cell',
    props: {
        slotName: { type: String, required: true },
        slotData: { type: Object, required: true },
        isDragging: { type: Boolean, required: true },
        search: { type: String, required: true },
        filterByColIndexes: { type: Array, required: true },
    },
    computed: {
        draggableClass() {
            return 'cursor--grab no-userSelect'
        },
        isCellDraggable() {
            return this.$typy(this.slotData, 'header.draggable').safeBoolean
        },
        highlighterData() {
            return {
                keyword:
                    this.filterByColIndexes.includes(this.slotData.colIdx) ||
                    !this.filterByColIndexes.length
                        ? this.search
                        : '',
                txt: this.slotData.cell,
            }
        },
        useCellSlot() {
            return this.slotData.header.useCellSlot
        },
    },
    created() {
        this.debouncedShowTooltip = this.$helpers.lodash.debounce(() => {
            this.SET_TRUNCATE_TOOLTIP_ITEM(
                this.$typy(this.$refs, 'cell.scrollWidth').safeNumber >
                    this.$typy(this.$refs, 'cell.clientWidth').safeNumber
                    ? { txt: this.slotData.cell, activatorID: this.slotData.activatorID }
                    : null
            )
        }, 300)
    },
    methods: {
        ...mapMutations({ SET_TRUNCATE_TOOLTIP_ITEM: 'mxsApp/SET_TRUNCATE_TOOLTIP_ITEM' }),
        mouseenterHandler() {
            this.debouncedShowTooltip()
        },
        mouseenter() {
            if (!this.isDragging) this.debouncedShowTooltip()
        },
        mousedown(e) {
            if (this.isCellDraggable) this.$emit('mousedown', e)
        },
        contextmenu(e) {
            e.preventDefault()
            this.$emit('on-cell-right-click', {
                e,
                row: this.slotData.rowData,
                cell: this.slotData.cell,
                activatorID: this.slotData.activatorID,
            })
        },
    },
}
</script>
