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
                        minWidth: $help.handleAddPxUnit(headerWidthMap[0]),
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
                    <slot
                        :name="`vertical-header-${h.text}`"
                        :data="{
                            header: h,
                            maxWidth: $typy(headerWidthMap[0]).safeNumber - 24,
                            colIdx: i,
                        }"
                    >
                        <truncate-string
                            :text="`${h.text}`"
                            :maxWidth="$typy(headerWidthMap[0]).safeNumber - 24"
                        />
                    </slot>
                </div>
                <div
                    :id="genCellID({ rowIdx, colIdx: 1 })"
                    :key="`${h.text}_${headerWidthMap[1]}_1`"
                    class="td no-border px-3"
                    :style="{
                        minWidth: $help.handleAddPxUnit(headerWidthMap[1]),
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
                    <slot :name="h.text" :data="{ cell: row[i], header: h, colIdx: i }" />
                </div>
                <div
                    v-if="!isYOverflowed"
                    :style="{ minWidth: `${scrollBarThicknessOffset}px`, height: lineHeight }"
                    class="dummy-cell color border-right-table-border"
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
 * Change Date: 2025-12-13
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
    },
}
</script>
<style lang="scss" scoped>
.tr-vertical-group {
    .tr:last-of-type {
        .td {
            border-bottom: thin solid $separator !important;
        }
    }
}
</style>
