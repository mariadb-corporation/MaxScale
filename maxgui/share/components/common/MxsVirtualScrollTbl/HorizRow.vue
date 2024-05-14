<template>
    <div
        class="tr"
        :class="{
            'tr--selected': isRowSelected(row),
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
            <v-checkbox
                :input-value="isRowSelected(row)"
                dense
                class="v-checkbox--scale-reduce ma-0 pa-0"
                primary
                hide-details
                @change="
                    val =>
                        val
                            ? selectedRows.push(row)
                            : selectedRows.splice(getSelectedRowIdx(row), 1)
                "
            />
        </div>
        <template v-for="(h, colIdx) in tableHeaders">
            <!-- dependency keys to force a rerender -->
            <table-cell
                v-if="!h.hidden"
                :key="`${h.text}_${headerWidthMap[colIdx]}_${colIdx}`"
                :style="{
                    height: lineHeight,
                    minWidth: $helpers.handleAddPxUnit(headerWidthMap[colIdx]),
                }"
                :slotName="h.text"
                :slotData="{
                    rowData: row,
                    rowIdx,
                    cell: row[colIdx],
                    colIdx,
                    header: h,
                    maxWidth: $typy(cellContentWidthMap[colIdx]).safeNumber,
                    activatorID: genActivatorID(`${rowIdx}-${colIdx}`),
                    isDragging,
                    search,
                }"
                v-on="$listeners"
            >
                <template v-for="(_, slot) in $scopedSlots" v-slot:[slot]="props">
                    <slot :name="slot" v-bind="props" />
                </template>
            </table-cell>
        </template>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-12
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
@on-cell-right-click: { e: event, row:[], cell:string, activatorID:string }
@on-ungroup: Emit when the header has groupable key
*/
import TableCell from './TableCell.vue'

export default {
    name: 'horiz-row',
    components: { TableCell },
    props: {
        row: { type: [Array, Object], required: true },
        rowIdx: { type: Number, required: true },
        selectedTblRows: { type: Array, required: true }, //sync
        areHeadersHidden: { type: Boolean, required: true },
        tableHeaders: { type: Array, required: true },
        lineHeight: { type: String, required: true },
        showSelect: { type: Boolean, required: true },
        checkboxColWidth: { type: Number, required: true },
        activeRow: { type: [Array, Object], required: true },
        genActivatorID: { type: Function, required: true },
        headerWidthMap: { type: Object, required: true },
        cellContentWidthMap: { type: Object, required: true },
        isDragging: { type: Boolean, default: true },
        search: { type: String, required: true },
    },
    computed: {
        selectedRows: {
            get() {
                return this.selectedTblRows
            },
            set(value) {
                this.$emit('update:selectedTblRows', value)
            },
        },
    },
    methods: {
        // SELECT feat
        /**
         * @param {Array} row - row array
         * @returns {Number} - returns index of row array in selectedRows
         */
        getSelectedRowIdx(row) {
            return this.selectedRows.findIndex(ele => this.$helpers.lodash.isEqual(ele, row))
        },
        /**
         * @param {Array} row - row array
         * @returns {Boolean} - returns true if row is found in selectedTblRows
         */
        isRowSelected(row) {
            return this.getSelectedRowIdx(row) === -1 ? false : true
        },
    },
}
</script>
