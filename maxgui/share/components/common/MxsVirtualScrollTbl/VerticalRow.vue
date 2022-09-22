<template>
    <div class="tr-vertical-group d-flex flex-column">
        <template v-for="(h, i) in tableHeaders">
            <div
                v-if="!h.hidden"
                :key="`${h.text}_${i}`"
                class="tr align-center"
                :style="{ height: lineHeight }"
            >
                <div
                    :id="genCellID({ rowIdx, colIdx: 0 })"
                    :key="`${h.text}_${headerWidthMap[0]}_0`"
                    class="td border-bottom-none px-3"
                    :style="{
                        minWidth: $helpers.handleAddPxUnit(headerWidthMap[0]),
                        lineHeight,
                        height: lineHeight,
                    }"
                    @contextmenu.prevent="
                        e =>
                            $emit('on-cell-right-click', {
                                e,
                                row,
                                cell: row[i],
                                cellID: genCellID({ rowIdx, colIdx: 0 }),
                            })
                    "
                >
                    <!-- vertical-row header slot -->
                    <slot
                        :name="`vertical-header-${h.text}`"
                        :data="{
                            header: h,
                            maxWidth: $typy(cellContentMaxWidth[0]).safeNumber,
                            colIdx: i,
                        }"
                    >
                        <mxs-truncate-str
                            :text="`${h.text}`"
                            :maxWidth="$typy(cellContentMaxWidth[0]).safeNumber"
                        />
                    </slot>
                </div>
                <div
                    :id="genCellID({ rowIdx, colIdx: 1 })"
                    :key="`${h.text}_${headerWidthMap[1]}_1`"
                    class="td no-border px-3"
                    :style="{
                        minWidth: $helpers.handleAddPxUnit(headerWidthMap[1]),
                        lineHeight,
                        height: lineHeight,
                    }"
                    @contextmenu.prevent="
                        e =>
                            $emit('on-cell-right-click', {
                                e,
                                row,
                                cell: row[i],
                                cellID: genCellID({ rowIdx, colIdx: 1 }),
                            })
                    "
                >
                    <!-- vertical-row cell slot -->
                    <slot
                        :name="h.text"
                        :data="{
                            rowData: row,
                            cell: row[i],
                            header: h,
                            maxWidth: $typy(cellContentMaxWidth[1]).safeNumber,
                            colIdx: i,
                            rowIdx,
                        }"
                    >
                        <mxs-truncate-str
                            :text="`${row[i]}`"
                            :maxWidth="$typy(cellContentMaxWidth[1]).safeNumber"
                        />
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
        genCellID: { type: Function, required: true },
        cellContentMaxWidth: { type: Object, required: true },
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
