<template>
    <div class="tr tr--group" :style="{ lineHeight }">
        <div
            class="d-flex align-center td pl-1 pr-3 mxs-color-helper border-right-table-border"
            :style="{
                height: lineHeight,
                minWidth: `${maxWidth}px`,
                maxWidth: `${maxWidth}px`,
            }"
        >
            <v-btn width="24" height="24" class="arrow-toggle" icon @click="toggleRowGroup">
                <v-icon
                    :class="[isCollapsed ? 'rotate-right' : 'rotate-down']"
                    size="24"
                    color="navigation"
                >
                    mdi-chevron-down
                </v-icon>
            </v-btn>
            <!-- checkbox for selecting/deselecting all items of the group -->
            <v-checkbox
                v-if="showSelect"
                :input-value="isRowGroupSelected"
                class="v-checkbox--mariadb-xs ma-0 pa-0"
                primary
                hide-details
                @change="handleSelectGroup"
            />
            <div
                class="tr--group__content d-inline-flex align-center"
                :style="{ maxWidth: `${maxVisWidth}px` }"
            >
                <mxs-truncate-str
                    class="font-weight-bold"
                    :tooltipItem="{ txt: `${row.groupBy}` }"
                    :maxWidth="maxVisWidth * 0.15"
                />

                <span class="d-inline-block val-separator mr-4">:</span>
                <mxs-truncate-str
                    v-mxs-highlighter="highlighterData"
                    :tooltipItem="{ txt: `${row.value}` }"
                    :maxWidth="maxVisWidth * 0.85"
                />
            </div>
            <mxs-tooltip-btn
                btnClass="ml-2"
                width="24"
                height="24"
                icon
                color="primary"
                @click="handleUngroup"
            >
                <template v-slot:btn-content>
                    <v-icon size="10"> $vuetify.icons.mxs_close</v-icon>
                </template>
                {{ $mxs_t('ungroup') }}
            </mxs-tooltip-btn>
        </div>
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
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
@on-ungroup: Emit when the ungroup button is clicked
 */
export default {
    name: 'row-group',
    props: {
        collapsedRowGroups: { type: Array, required: true }, //sync
        selectedTblRows: { type: Array, required: true }, //sync
        selectedGroupRows: { type: Array, required: true }, //sync
        row: { type: Object, required: true },
        tableData: { type: Array, required: true },
        isCollapsed: { type: Boolean, required: true },
        boundingWidth: { type: Number, required: true },
        lineHeight: { type: String, required: true },
        showSelect: { type: Boolean, required: true },
        maxWidth: { type: Number, required: true },
        filterByColIndexes: { type: Array, required: true },
        search: { type: String, required: true },
    },
    computed: {
        maxVisWidth() {
            /** A workaround to get maximum width of row group header
             * 18 is the total width of padding and border of table
             * 24 is the width of toggle button
             * 32 is the width of ungroup button
             */
            return this.boundingWidth - 18 - 24 - 32
        },
        collapsedGroups: {
            get() {
                return this.collapsedRowGroups
            },
            set(value) {
                this.$emit('update:collapsedRowGroups', value)
            },
        },
        selectedGroups: {
            get() {
                return this.selectedGroupRows
            },
            set(value) {
                this.$emit('update:selectedGroupRows', value)
            },
        },
        selectedGroupItems: {
            get() {
                return this.selectedTblRows
            },
            set(value) {
                this.$emit('update:selectedTblRows', value)
            },
        },
        highlighterData() {
            return {
                keyword:
                    this.filterByColIndexes.includes(this.row.groupByColIdx) ||
                    !this.filterByColIndexes.length
                        ? this.search
                        : '',
                txt: this.row.value,
            }
        },
        selectedGroupIdx() {
            return this.selectedGroups.findIndex(ele => this.$helpers.lodash.isEqual(ele, this.row))
        },
        areGroupItemsSelected() {
            return this.getGroupItems().every(item => this.selectedGroupItems.includes(item))
        },
        isRowGroupSelected() {
            return this.areGroupItemsSelected ||
                (this.selectedGroupIdx !== -1 && this.areGroupItemsSelected)
                ? true
                : false
        },
    },
    methods: {
        //Row group toggle feat
        toggleRowGroup() {
            const targetIdx = this.collapsedGroups.findIndex(r =>
                this.$helpers.lodash.isEqual(this.row, r)
            )
            if (targetIdx >= 0)
                this.collapsedGroups = [
                    ...this.collapsedGroups.slice(0, targetIdx),
                    ...this.collapsedGroups.slice(targetIdx + 1),
                ]
            else this.collapsedGroups = [...this.collapsedGroups, this.row]
        },
        handleUngroup() {
            this.collapsedGroups = []
            this.$emit('on-ungroup')
        },
        //SELECT feat
        /**
         * This method returns rows belonged to row group
         * @returns {Array} - returns 2d array
         */
        getGroupItems() {
            const { isEqual } = this.$helpers.lodash
            const targetIdx = this.tableData.findIndex(ele => isEqual(ele, this.row))
            let items = []
            let i = targetIdx + 1
            while (i !== -1) {
                if (Array.isArray(this.tableData[i])) {
                    items.push(this.tableData[i])
                    i++
                } else i = -1
            }
            return items
        },
        /**
         * @param {Boolean} v - is row group selected
         */
        handleSelectGroupItems(v) {
            const { isEqual, xorWith, differenceWith } = this.$helpers.lodash
            let groupItems = this.getGroupItems()
            let newSelectedItems = []
            if (v) newSelectedItems = xorWith(this.selectedGroupItems, groupItems, isEqual)
            else newSelectedItems = differenceWith(this.selectedGroupItems, groupItems, isEqual)
            this.selectedGroupItems = newSelectedItems
        },
        /**
         * @param {Boolean} v - is row group selected
         * @emits update:selectedGroupRows - Emits event with new data for selectedGroupRows
         */
        handleSelectGroup(v) {
            if (v) this.selectedGroups.push(this.row)
            else this.selectedGroups.splice(this.selectedGroupIdx, 1)
            this.handleSelectGroupItems(v)
        },
    },
}
</script>
