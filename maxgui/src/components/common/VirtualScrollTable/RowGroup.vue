<template>
    <div class="tr tr--group" :style="{ lineHeight }">
        <div
            class="d-flex align-center td pl-1 pr-3 td-col-span"
            :style="{
                height: lineHeight,
                width: '100%',
            }"
        >
            <v-btn width="24" height="24" class="arrow-toggle" icon @click="toggleRowGroup">
                <v-icon
                    :class="[isCollapsed ? 'arrow-right' : 'arrow-down']"
                    size="24"
                    color="deep-ocean"
                >
                    $expand
                </v-icon>
            </v-btn>

            <v-checkbox
                v-if="showSelect"
                :input-value="isRowGroupSelected"
                dense
                class="checkbox--scale-reduce ma-0 pa-0"
                primary
                hide-details
                @change="handleSelectRowGroup"
            />

            <div
                class="tr--group__content d-inline-flex align-center"
                :style="{ maxWidth: `${maxRowGroupWidth}px` }"
            >
                <truncate-string
                    class="font-weight-bold"
                    :text="`${row.groupBy}`"
                    :maxWidth="maxRowGroupWidth * 0.15"
                />
                <span class="d-inline-block val-separator mr-4">:</span>
                <truncate-string :text="`${row.value}`" :maxWidth="maxRowGroupWidth * 0.85" />
            </div>

            <v-tooltip
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
            >
                <template v-slot:activator="{ on }">
                    <v-btn
                        class="ml-2"
                        width="24"
                        height="24"
                        icon
                        v-on="on"
                        @click="handleUngroup"
                    >
                        <v-icon size="10" color="deep-ocean"> $vuetify.icons.close</v-icon>
                    </v-btn>
                </template>
                <span>{{ $t('ungroup') }}</span>
            </v-tooltip>
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
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'row-group',
    props: {
        value: { type: Array, required: true },
        row: { type: Object, required: true },
        tableRows: { type: Array, required: true },
        collapsedRowGroups: { type: Array, required: true },
        isCollapsed: { type: Boolean, required: true },
        selectedItems: { type: Array, required: true },
        showSelect: { type: Boolean, required: true },
        boundingWidth: { type: Number, required: true },
        lineHeight: { type: String, required: true },
    },
    computed: {
        selectedGroupItems: {
            get() {
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
        maxRowGroupWidth() {
            /** A workaround to get maximum width of row group header
             * 17 is the total width of padding and border of table
             * 28 is the width of toggle button
             * 32 is the width of ungroup button
             */
            return this.boundingWidth - this.$help.getScrollbarWidth() - 17 - 28 - 32
        },
        getSelectedRowGroupIdx() {
            return this.selectedGroupItems.findIndex(ele =>
                this.$help.lodash.isEqual(ele, this.row)
            )
        },
        isRowGroupSelected() {
            return this.getSelectedRowGroupIdx === -1 ? false : true
        },
    },
    methods: {
        /**
         * @emits update-collapsed-row-groups - Emits event with new data for collapsedRowGroups
         */
        toggleRowGroup() {
            const targetIdx = this.collapsedRowGroups.findIndex(r =>
                this.$help.lodash.isEqual(this.row, r)
            )
            if (targetIdx >= 0)
                this.$emit('update-collapsed-row-groups', [
                    ...this.collapsedRowGroups.slice(0, targetIdx),
                    ...this.collapsedRowGroups.slice(targetIdx + 1),
                ])
            else this.$emit('update-collapsed-row-groups', [...this.collapsedRowGroups, this.row])
        },

        /**
         * This method returns rows belonged to row group
         * @param {Object} row - row group object
         * @returns {Array} - returns 2d array
         */
        getGroupItems(row) {
            const { isEqual } = this.$help.lodash
            const targetIdx = this.tableRows.findIndex(ele => isEqual(ele, row))
            let items = []
            let i = targetIdx + 1
            while (i !== -1) {
                if (Array.isArray(this.tableRows[i])) {
                    items.push(this.tableRows[i])
                    i++
                } else i = -1
            }
            return items
        },

        /**
         * @param {Boolean} v - is row group selected
         * @emits update-selected-items - Emits event with new data for selectedItems
         */
        handleSelectGroupItems(v) {
            const { isEqual, xorWith, differenceWith } = this.$help.lodash
            let groupItems = this.getGroupItems(this.row)
            let newSelectedItems = []
            if (v) newSelectedItems = xorWith(this.selectedItems, groupItems, isEqual)
            else newSelectedItems = differenceWith(this.selectedItems, groupItems, isEqual)
            this.$emit('update-selected-items', newSelectedItems)
        },

        handleSelectRowGroup(v) {
            if (v) this.selectedGroupItems.push(this.row)
            else this.selectedGroupItems.splice(this.getSelectedRowGroupIdx, 1)
            this.handleSelectGroupItems(v)
        },

        handleUngroup() {
            this.$emit('update-collapsed-row-groups', [])
            this.$emit('on-ungroup')
        },
    },
}
</script>

<style lang="scss" scoped>
.tr {
    &--group {
        .td {
            background-color: #f2fcff !important;
        }
    }
}
</style>
