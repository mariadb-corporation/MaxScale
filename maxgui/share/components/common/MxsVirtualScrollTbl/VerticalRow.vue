<template>
    <div class="tr-vertical-group d-flex flex-column">
        <template v-for="(h, colIdx) in tableHeaders">
            <div
                v-if="!h.hidden"
                :key="`${h.text}_${colIdx}`"
                class="tr align-center"
                :style="{ height: lineHeight }"
            >
                <div
                    :id="genHeaderColID(colIdx)"
                    class="td border-bottom-none px-3"
                    :style="headerColStyle"
                    @contextmenu.prevent="
                        ctxMenuHandler({
                            e: $event,
                            cell: h.text,
                            activatorID: genHeaderColID(colIdx),
                        })
                    "
                >
                    <!-- vertical-row header slot -->
                    <slot
                        :name="`vertical-header-${h.text}`"
                        :data="{
                            header: h,
                            maxWidth: headerContentWidth,
                            colIdx,
                        }"
                    >
                        <mxs-truncate-str :text="`${h.text}`" :maxWidth="headerContentWidth" />
                    </slot>
                </div>
                <div
                    :id="genValueColID(colIdx)"
                    class="td no-border px-3"
                    :style="valueColStyle"
                    @contextmenu.prevent="
                        ctxMenuHandler({
                            e: $event,
                            cell: row[colIdx],
                            activatorID: genValueColID(colIdx),
                        })
                    "
                >
                    <!-- vertical-row cell slot -->
                    <slot
                        :name="h.text"
                        :data="{
                            rowData: row,
                            cell: row[colIdx],
                            header: h,
                            maxWidth: valueContentWidth,
                            colIdx,
                            rowIdx,
                        }"
                    >
                        <mxs-truncate-str :text="`${row[colIdx]}`" :maxWidth="valueContentWidth" />
                    </slot>
                </div>
                <div
                    v-if="!isYOverflowed"
                    :style="{ minWidth: `${scrollBarThicknessOffset}px`, height: lineHeight }"
                    class="dummy-cell mxs-color-helper border-right-table-border"
                />
            </div>
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
 * Change Date: 2026-09-06
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
export default {
    name: 'vertical-row',
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
        headerWidthMap: { type: Object, required: true },
        isYOverflowed: { type: Boolean, required: true },
        scrollBarThicknessOffset: { type: Number, required: true },
        genActivatorID: { type: Function, required: true },
        cellContentWidthMap: { type: Object, required: true },
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
                minWidth: this.$helpers.handleAddPxUnit(this.headerWidthMap[0]),
            }
        },
        valueColStyle() {
            return {
                ...this.baseColStyle,
                minWidth: this.$helpers.handleAddPxUnit(this.headerWidthMap[1]),
            }
        },
        headerContentWidth() {
            return this.$typy(this.cellContentWidthMap[0]).safeNumber
        },
        valueContentWidth() {
            return this.$typy(this.cellContentWidthMap[1]).safeNumber
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
