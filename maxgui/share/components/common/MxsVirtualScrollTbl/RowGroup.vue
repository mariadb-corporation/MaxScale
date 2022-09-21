<template>
    <div class="tr tr--group" :style="{ lineHeight }">
        <div
            class="d-flex align-center td pl-1 pr-3 mxs-color-helper border-right-table-border"
            :style="{
                height: lineHeight,
                width: '100%',
            }"
        >
            <v-btn width="24" height="24" class="arrow-toggle" icon @click="toggleRowGroup">
                <v-icon
                    :class="[isCollapsed ? 'rotate-right' : 'rotate-down']"
                    size="24"
                    color="deep-ocean"
                >
                    mdi-chevron-down
                </v-icon>
            </v-btn>
            <slot name="row-content-prepend"></slot>
            <div
                class="tr--group__content d-inline-flex align-center"
                :style="{ maxWidth: `${maxRowGroupWidth}px` }"
            >
                <mxs-truncate-str
                    class="font-weight-bold"
                    :text="`${row.groupBy}`"
                    :maxWidth="maxRowGroupWidth * 0.15"
                />
                <span class="d-inline-block val-separator mr-4">:</span>
                <mxs-truncate-str :text="`${row.value}`" :maxWidth="maxRowGroupWidth * 0.85" />
            </div>
            <mxs-tooltip-btn btnClass="ml-2" width="24" height="24" icon @click="handleUngroup">
                <template v-slot:btn-content>
                    <v-icon size="10" color="deep-ocean"> $vuetify.icons.mxs_close</v-icon>
                </template>
                {{ $mxs_t('ungroup') }}
            </mxs-tooltip-btn>
        </div>
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
    name: 'row-group',
    props: {
        row: { type: Object, required: true },
        collapsedRowGroups: { type: Array, required: true },
        isCollapsed: { type: Boolean, required: true },
        boundingWidth: { type: Number, required: true },
        lineHeight: { type: String, required: true },
        scrollBarThicknessOffset: { type: Number, required: true },
    },
    computed: {
        maxRowGroupWidth() {
            /** A workaround to get maximum width of row group header
             * 17 is the total width of padding and border of table
             * 28 is the width of toggle button
             * 32 is the width of ungroup button
             */
            return this.boundingWidth - this.scrollBarThicknessOffset - 17 - 28 - 32
        },
    },
    methods: {
        /**
         * @emits update-collapsed-row-groups - Emits event with new data for collapsedRowGroups
         */
        toggleRowGroup() {
            const targetIdx = this.collapsedRowGroups.findIndex(r =>
                this.$helpers.lodash.isEqual(this.row, r)
            )
            if (targetIdx >= 0)
                this.$emit('update-collapsed-row-groups', [
                    ...this.collapsedRowGroups.slice(0, targetIdx),
                    ...this.collapsedRowGroups.slice(targetIdx + 1),
                ])
            else this.$emit('update-collapsed-row-groups', [...this.collapsedRowGroups, this.row])
        },

        handleUngroup() {
            this.$emit('update-collapsed-row-groups', [])
            this.$emit('on-ungroup')
        },
    },
}
</script>
