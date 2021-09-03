<template>
    <div class="virtual-table" :class="{ 'no-userSelect': isResizing }">
        <table-header
            ref="tableHeader"
            :isVertTable="isVertTable"
            :headers="visHeaders"
            :boundingWidth="boundingWidth"
            :headerStyle="headerStyle"
            :currRowsLen="currRowsLen"
            :showSelect="showSelect"
            :isAllselected="isAllselected"
            :indeterminate="indeterminate"
            @get-header-width-map="headerWidthMap = $event"
            @is-resizing="isResizing = $event"
            @on-sorting="onSorting"
            @on-group="onGrouping"
            @toggle-select-all="handleSelectAll"
        />
        <v-virtual-scroll
            v-if="tableRows.length && visHeaders.length"
            ref="vVirtualScroll"
            :bench="isVertTable ? 1 : benched"
            :items="tableRows"
            :height="tbodyHeight"
            :item-height="rowHeight"
            class="tbody"
            @scroll.native="scrolling"
        >
            <template v-slot:default="{ item: row }">
                <div v-if="isVertTable" class="tr-vertical-group d-flex flex-column">
                    <template v-for="(h, i) in visHeaders">
                        <div
                            :key="`${h.text}_${i}`"
                            class="tr align-center"
                            :style="{ height: lineHeight }"
                        >
                            <div
                                :key="`${h.text}_${headerWidthMap[0]}_0`"
                                class="td fill-height d-flex align-center border-bottom-none px-3"
                                :style="{
                                    minWidth: $help.handleAddPxUnit(headerWidthMap[0]),
                                }"
                            >
                                <truncate-string
                                    :text="`${h.text}`.toUpperCase()"
                                    :maxWidth="$typy(headerWidthMap[0]).safeNumber - 24"
                                />
                            </div>
                            <div
                                :key="`${h.text}_${headerWidthMap[1]}_1`"
                                class="td fill-height d-flex align-center no-border px-3"
                                :style="{
                                    minWidth: $help.handleAddPxUnit(headerWidthMap[1]),
                                }"
                            >
                                <slot
                                    :name="h.text"
                                    :data="{
                                        cell: row[i],
                                        header: h,
                                        maxWidth: cellMaxWidth(1),
                                    }"
                                >
                                    <truncate-string
                                        :text="`${row[i]}`"
                                        :maxWidth="cellMaxWidth(1)"
                                    />
                                </slot>
                            </div>
                        </div>
                    </template>
                </div>
                <div v-else-if="isGroupRow(row)" class="tr tr--group" :style="{ lineHeight }">
                    <div
                        class="d-flex align-center td pl-1 pr-3 td-col-span"
                        :style="{
                            height: lineHeight,
                            width: '100%',
                        }"
                    >
                        <v-btn
                            width="24"
                            height="24"
                            class="arrow-toggle mr-1"
                            icon
                            @click="() => toggleRowGroup(row)"
                        >
                            <v-icon
                                :class="[isRowCollapsed(row) ? 'arrow-right' : 'arrow-down']"
                                size="24"
                                color="deep-ocean"
                            >
                                $expand
                            </v-icon>
                        </v-btn>
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
                            <truncate-string
                                :text="`${row.value}`"
                                :maxWidth="maxRowGroupWidth * 0.85"
                            />
                        </div>

                        <v-btn class="ml-2" width="24" height="24" icon @click="handleUngroup">
                            <v-icon size="10" color="deep-ocean"> $vuetify.icons.close</v-icon>
                        </v-btn>
                    </div>
                </div>
                <div v-else class="tr" :style="{ lineHeight }">
                    <div
                        v-if="showSelect"
                        class="td px-3"
                        :style="{
                            height: lineHeight,
                            maxWidth: '50px',
                            minWidth: '50px',
                        }"
                    >
                        <v-checkbox
                            :input-value="isRowSelected(row)"
                            dense
                            class="checkbox--scale-reduce ma-0"
                            primary
                            hide-details
                            @change="
                                val =>
                                    val
                                        ? selectedItems.push(row)
                                        : selectedItems.splice(getSelectedRowIdx(row), 1)
                            "
                        />
                    </div>
                    <template v-for="(h, i) in visHeaders">
                        <!-- dependency keys to force a rerender -->
                        <div
                            v-if="!h.hidden"
                            :key="`${h.text}_${headerWidthMap[i]}_${i}`"
                            class="td px-3"
                            :style="{
                                height: lineHeight,
                                minWidth: $help.handleAddPxUnit(headerWidthMap[i]),
                            }"
                        >
                            <slot
                                :name="h.text"
                                :data="{
                                    cell: row[i],
                                    header: h,
                                    maxWidth: cellMaxWidth(i),
                                }"
                            >
                                <truncate-string :text="`${row[i]}`" :maxWidth="cellMaxWidth(i)" />
                            </slot>
                        </div>
                    </template>
                </div>
            </template>
        </v-virtual-scroll>
        <div
            v-else-if="!tableRows.length"
            class="tr"
            :style="{ lineHeight, height: `${height - itemHeight}px` }"
        >
            <div class="td px-3 d-flex justify-center flex-grow-1">
                {{ $t('$vuetify.noDataText') }}
            </div>
        </div>
        <div v-if="isResizing" class="resizing-mask" />
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
import TableHeader from './TableHeader'
export default {
    name: 'virtual-scroll-table',
    components: {
        'table-header': TableHeader,
    },
    props: {
        headers: {
            type: Array,
            validator: arr => {
                if (!arr.length) return true
                else return arr.filter(item => 'text' in item).length === arr.length
            },
            required: true,
        },
        rows: { type: Array, required: true },
        height: { type: Number, required: true },
        itemHeight: { type: Number, required: true },
        boundingWidth: { type: Number, required: true },
        benched: { type: Number, required: true },
        isVertTable: { type: Boolean, default: false },
        showSelect: { type: Boolean, default: false },
    },
    data() {
        return {
            headerWidthMap: {},
            headerStyle: {},
            isResizing: false,
            lastScrollTop: 0,
            idxOfSortingCol: -1,
            isDesc: false,
            //GroupBy feat states
            activeGroupBy: '',
            activeGroupByHeader: null,
            idxOfGroupCol: -1,
            collapsedRowGroups: [],
            totalGroupsLength: 0,
            selectedItems: [],
        }
    },
    computed: {
        lineHeight() {
            return `${this.itemHeight}px`
        },
        tbodyHeight() {
            return this.height - this.itemHeight
        },
        rowHeight() {
            return this.isVertTable ? `${this.itemHeight * this.headers.length}px` : this.itemHeight
        },
        maxRowGroupWidth() {
            /** A workaround to get maximum width of row group header
             * 17 is the total width of padding and border of table
             * 28 is the width of toggle button
             * 32 is the width of ungroup button
             */
            return this.boundingWidth - this.$help.getScrollbarWidth() - 17 - 28 - 32
        },
        tableRows() {
            /* Use JSON.stringify as it's faster comparing to lodash cloneDeep
             * Though it comes with pitfalls and should be used for ajax data
             */
            let rows = JSON.parse(JSON.stringify(this.rows))
            if (this.idxOfSortingCol !== -1) this.handleSort(rows)
            if (this.idxOfGroupCol !== -1 && !this.isVertTable) rows = this.handleGroupRows(rows)
            return rows
        },
        visHeaders() {
            if (this.idxOfGroupCol === -1) return this.headers
            return this.headers.map(h =>
                this.activeGroupBy === h.text ? { ...h, hidden: true } : h
            )
        },
        currRowsLen() {
            return this.tableRows.length - this.totalGroupsLength
        },
        isAllselected() {
            if (!this.selectedItems.length) return false
            return this.selectedItems.length === this.currRowsLen
        },
        indeterminate() {
            if (!this.selectedItems.length) return false
            return !this.isAllselected && this.selectedItems.length < this.currRowsLen
        },
    },
    watch: {
        selectedItems: {
            deep: true,
            handler(v) {
                this.$emit('item-selected', v)
                this.$emit('current-rows-length', this.currRowsLen)
            },
        },
        rows: {
            deep: true,
            handler(v, oV) {
                // Clear selectedItems once rows value changes
                if (!this.$help.lodash.isEqual(v, oV)) this.selectedItems = []
            },
        },
        isVertTable(v) {
            // clear selected items
            if (v) this.selectedItems = []
        },
    },
    activated() {
        /**
         * activated hook is triggered when this component is placed
         * as a children component or nested component of keep-alive.
         * For some reason, the last scrollTop position isn't preserved in
         * v-virtual-scroll component. This is a workaround to manually
         * scroll the content to lastScrollTop value
         */
        if (this.$refs.vVirtualScroll) {
            this.$refs.vVirtualScroll.$el.scrollTop = 1 // in case lastScrollTop === 0
            this.$refs.vVirtualScroll.$el.scrollTop = this.lastScrollTop
        }
    },
    methods: {
        scrolling(event) {
            const ele = event.currentTarget || event.target
            //make table header to "scrollX" as well
            this.headerStyle = {
                ...this.headerStyle,
                position: 'relative',
                left: `-${ele.scrollLeft}px`,
            }
            this.lastScrollTop = ele.scrollTop
            if (ele && ele.scrollHeight - ele.scrollTop === ele.clientHeight)
                this.$emit('scroll-end')
        },
        cellMaxWidth(i) {
            return this.$typy(this.headerWidthMap[i]).safeNumber - 24
        },
        /**
         * @param {String} payload.sortBy  sort by header name
         * @param {Boolean} payload.isDesc  isDesc
         */
        onSorting({ sortBy, isDesc }) {
            this.idxOfSortingCol = this.visHeaders.findIndex(h => h.text === sortBy)
            this.isDesc = isDesc
        },
        /**
         * @param {Array} rows - 2d array to be sorted
         */
        handleSort(rows) {
            rows.sort((a, b) => {
                if (this.isDesc) return b[this.idxOfSortingCol] < a[this.idxOfSortingCol] ? -1 : 1
                else return a[this.idxOfSortingCol] < b[this.idxOfSortingCol] ? -1 : 1
            })
        },
        /** This groups 2d array with same value at provided index to a Map
         * @param {Array} payload.rows - 2d array to be grouped into a Map
         * @param {Number} payload.idx - col index of the inner array
         * @returns {Map} - returns map with value as key and value is a matrix (2d array)
         */
        groupValues({ rows, idx }) {
            let map = new Map()
            rows.forEach(row => {
                const key = row[idx]
                let matrix = map.get(key) || [] // assign an empty arr if not found
                matrix.push(row)
                map.set(key, matrix)
            })
            return map
        },
        handleGroupRows(rows) {
            let rowMap = this.groupValues({ rows, idx: this.idxOfGroupCol })
            if (this.headers[this.idxOfGroupCol].hasCustomGroup) {
                const data = {
                    rows,
                    idx: this.idxOfGroupCol,
                    header: this.headers[this.idxOfGroupCol],
                }
                // emit custom-group and provide callback to assign return value of custom-group
                this.$emit('custom-group', data, map => (rowMap = map))
            }
            this.assignTotalGroupsLength(rowMap.size)
            let groupRows = []
            for (const [key, value] of rowMap) {
                groupRows.push({
                    groupBy: this.activeGroupBy,
                    value: key,
                    groupLength: value.length,
                })
                groupRows = [...groupRows, ...value]
            }
            return this.handleFilterGroupRows(groupRows)
        },
        /**
         * @param {Object} payload.activeGroupBy - header name
         * @param {Number} payload.i - header index
         */
        onGrouping({ header, activeGroupBy }) {
            this.activeGroupBy = activeGroupBy
            this.activeGroupByHeader = header
            this.idxOfGroupCol = this.headers.findIndex(h => h.text === activeGroupBy)
            this.$emit('is-grouping', Boolean(activeGroupBy))
        },
        /**
         * @param {Array} groupRows - rows that have been grouped
         * @returns {Array} - filtered rows by collapsedRowGroups values
         */
        handleFilterGroupRows(groupRows) {
            let hiddenRowIdxs = []
            if (this.collapsedRowGroups.length) {
                for (const [i, r] of groupRows.entries()) {
                    if (this.isRowCollapsed(r)) {
                        hiddenRowIdxs = [
                            ...hiddenRowIdxs,
                            ...Array(r.groupLength)
                                .fill()
                                .map((_, n) => n + i + 1),
                        ]
                    }
                }
            }
            return groupRows.filter((_, i) => !hiddenRowIdxs.includes(i))
        },
        /**
         * totalGroupsLength is used mainly to calculate total number of rows (row groups are not counted)
         * @param {Number} l - Group length
         */
        assignTotalGroupsLength(l) {
            this.totalGroupsLength = l
        },
        /**
         * @param {Object|Array} row - row to check
         * @returns {Boolean} - return whether this is a group row or not
         */
        isGroupRow(row) {
            return this.$typy(row).isObject
        },
        isRowCollapsed(row) {
            const targetIdx = this.collapsedRowGroups.findIndex(r =>
                this.$help.lodash.isEqual(row, r)
            )
            return targetIdx === -1 ? false : true
        },
        /**
         * @param {Number} rowIdx - row group index
         */
        toggleRowGroup(row) {
            const targetIdx = this.collapsedRowGroups.findIndex(r =>
                this.$help.lodash.isEqual(row, r)
            )
            if (targetIdx >= 0) this.collapsedRowGroups.splice(targetIdx, 1)
            else this.collapsedRowGroups.push(row)
        },
        handleUngroup() {
            this.collapsedRowGroups = []
            this.$refs.tableHeader.handleToggleGroup(this.activeGroupByHeader)
        },
        getSelectedRowIdx(row) {
            return this.selectedItems.findIndex(ele => this.$help.lodash.isEqual(ele, row))
        },
        isRowSelected(row) {
            return this.getSelectedRowIdx(row) === -1 ? false : true
        },
        handleSelectAll(v) {
            // don't select group row
            if (v) this.selectedItems = this.tableRows.filter(row => Array.isArray(row))
            else this.selectedItems = []
        },
    },
}
</script>

<style lang="scss" scoped>
::v-deep.virtual-table {
    width: 100%;
    .tbody {
        // Always show scrollbar to make table header and table cell align vertically
        overflow: scroll;
        .tr {
            display: flex;
            .td {
                font-size: 0.875rem;
                color: $navigation;
                border-bottom: thin solid $table-border;
                border-right: thin solid $table-border;
                background: $background;
                &:first-of-type {
                    border-left: thin solid $table-border;
                }
                &:last-of-type {
                    border-right: none;
                }
            }
            &:hover {
                .td {
                    background: $table-row-hover;
                }
            }
            &:active {
                .td {
                    background: #f2fcff;
                }
            }
            &--group {
                .td {
                    background-color: #f2fcff !important;
                }
            }
        }
        .tr-vertical-group {
            .tr {
                &:last-of-type {
                    border-bottom: thin solid $table-border;
                }
            }
        }
    }
}
</style>
