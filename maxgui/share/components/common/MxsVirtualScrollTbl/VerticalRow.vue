<template>
    <div class="tr-vertical-group d-flex flex-column">
        <template v-for="(h, colIdx) in tableHeaders">
            <div
                v-if="!h.hidden"
                :key="`${h.text}_${colIdx}`"
                class="tr align-center"
                :style="{ height: lineHeight }"
            >
                <!-- vertical-row header slot -->
                <table-cell
                    class="border-bottom-none"
                    :style="headerColStyle"
                    :slotName="`header-${h.text}`"
                    :slotData="{
                        rowData: row,
                        rowIdx,
                        cell: h.text,
                        colIdx,
                        header: h,
                        maxWidth: headerContentWidth,
                        activatorID: genHeaderColID(colIdx),
                    }"
                    :isDragging="isDragging"
                    :filterByColIndexes="filterByColIndexes"
                    v-on="$listeners"
                >
                    <template v-for="(_, slot) in $scopedSlots" v-slot:[slot]="props">
                        <slot :name="slot" v-bind="props" />
                    </template>
                </table-cell>
                <!-- vertical-row cell slot -->
                <table-cell
                    class="no-border"
                    :style="valueColStyle"
                    :slotName="h.text"
                    :slotData="{
                        rowData: row,
                        rowIdx,
                        cell: row[colIdx],
                        colIdx,
                        header: h,
                        maxWidth: valueContentWidth,
                        activatorID: genValueColID(colIdx),
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
            </div>
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
/**
 * In vertical-row mode, there are two html table headers which are `COLUMN` and `VALUE`.
 * `COLUMN`: render sql columns
 * `VALUE`: render sql column value
 * In this mode,
 * rowIdx: indicates to the row index
 * colIdx: indicates the column index in row (props)
 * 0: indicates the `COLUMN` index in the html table headers
 * 1: indicates the `VALUE` index in the html table headers
 */
import TableCell from '@share/components/common/MxsVirtualScrollTbl/TableCell.vue'

export default {
    name: 'vertical-row',
    components: { TableCell },
    props: {
        row: { type: Array, required: true },
        rowIdx: { type: Number, required: true },
        tableHeaders: {
            type: Array,
            validator: arr => {
                if (!arr.length) return true
                else return arr.filter(item => 'text' in item).length === arr.length
            },
            required: true,
        },
        lineHeight: { type: String, required: true },
        colWidths: { type: Array, required: true },
        genActivatorID: { type: Function, required: true },
        cellContentWidths: { type: Array, required: true },
        isDragging: { type: Boolean, default: true },
        search: { type: String, required: true },
        filterByColIndexes: { type: Array, required: true },
    },
    computed: {
        baseColStyle() {
            return {
                lineHeight: this.lineHeight,
                height: this.lineHeight,
            }
        },
        headerColStyle() {
            return {
                ...this.baseColStyle,
                minWidth: this.$helpers.handleAddPxUnit(this.colWidths[0]),
            }
        },
        valueColStyle() {
            return {
                ...this.baseColStyle,
                minWidth: this.$helpers.handleAddPxUnit(this.colWidths[1]),
            }
        },
        headerContentWidth() {
            return this.$typy(this.cellContentWidths, '[0]').safeNumber
        },
        valueContentWidth() {
            return this.$typy(this.cellContentWidths, '[1]').safeNumber
        },
    },
    methods: {
        genHeaderColID(colIdx) {
            return this.genActivatorID(`${this.rowIdx}-${colIdx}-${0}`)
        },
        genValueColID(colIdx) {
            return this.genActivatorID(`${this.rowIdx}-${colIdx}-${1}`)
        },
        ctxMenuHandler({ e, cell, activatorID }) {
            this.$emit('on-cell-right-click', {
                e,
                row: this.row,
                cell,
                activatorID,
            })
        },
    },
}
</script>
<style lang="scss" scoped>
.tr-vertical-group {
    .tr:last-of-type {
        .td {
            border-bottom: thin solid $table-border !important;
        }
    }
}
</style>
