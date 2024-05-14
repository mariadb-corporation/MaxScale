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
                class="v-checkbox--mariadb-xs ma-0 pa-0"
                primary
                hide-details
                @change="onChangeCheckbox"
                @click.stop
            />
        </div>
        <template v-for="(h, colIdx) in tableHeaders">
            <!-- dependency keys to force a rerender -->
            <table-cell
                v-if="!h.hidden"
                :key="`${h.text}_${colWidths[colIdx]}_${colIdx}`"
                :style="{
                    height: lineHeight,
                    minWidth: $helpers.handleAddPxUnit(colWidths[colIdx]),
                }"
                :slotName="h.text"
                :slotData="{
                    rowData: row,
                    rowIdx,
                    cell: row[colIdx],
                    colIdx,
                    header: h,
                    maxWidth: $typy(cellContentWidths[colIdx]).safeNumber,
                    activatorID: genActivatorID(`${rowIdx}-${colIdx}`),
                }"
                :isDragging="isDragging"
                :search="search"
                :filterByColIndexes="filterByColIndexes"
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
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import TableCell from '@share/components/common/MxsVirtualScrollTbl/TableCell.vue'

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
        colWidths: { type: Array, required: true },
        cellContentWidths: { type: Array, required: true },
        isDragging: { type: Boolean, default: true },
        search: { type: String, required: true },
        singleSelect: { type: Boolean, required: true },
        filterByColIndexes: { type: Array, required: true },
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
        onChangeCheckbox(val) {
            if (val) {
                if (this.singleSelect) this.selectedRows = [this.row]
                else this.selectedRows.push(this.row)
            } else this.selectedRows.splice(this.getSelectedRowIdx(this.row), 1)
        },
    },
}
</script>
