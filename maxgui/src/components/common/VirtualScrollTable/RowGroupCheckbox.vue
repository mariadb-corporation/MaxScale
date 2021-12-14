<template>
    <v-checkbox
        :input-value="isRowGroupSelected"
        dense
        class="checkbox--scale-reduce ma-0 pa-0"
        primary
        hide-details
        @change="handleSelectRowGroup"
    />
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
    name: 'row-group-checkbox',
    props: {
        value: { type: Array, required: true },
        row: { type: Object, required: true },
        tableRows: { type: Array, required: true },
        selectedItems: { type: Array, required: true },
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
        getSelectedRowGroupIdx() {
            return this.selectedGroupItems.findIndex(ele =>
                this.$help.lodash.isEqual(ele, this.row)
            )
        },
        isRowGroupSelected() {
            return this.areGroupItemsSelected ||
                (this.getSelectedRowGroupIdx !== -1 && this.areGroupItemsSelected)
                ? true
                : false
        },
        areGroupItemsSelected() {
            return this.getGroupItems().every(item => this.selectedItems.includes(item))
        },
    },
    methods: {
        /**
         * This method returns rows belonged to row group
         * @returns {Array} - returns 2d array
         */
        getGroupItems() {
            const { isEqual } = this.$help.lodash
            const targetIdx = this.tableRows.findIndex(ele => isEqual(ele, this.row))
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
            let groupItems = this.getGroupItems()
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
    },
}
</script>
