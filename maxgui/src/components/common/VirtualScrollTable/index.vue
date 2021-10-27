<template>
    <div
        class="virtual-table"
        :class="{ 'no-userSelect': isResizing }"
        :style="{ cursor: isResizing ? 'col-resize' : '' }"
    >
        <table-header
            ref="tableHeader"
            :isVertTable="isVertTable"
            :headers="tableHeaders"
            :boundingWidth="boundingWidth"
            :headerStyle="headerStyle"
            :rowsLength="rowsLength"
            :showSelect="showSelect"
            :isAllselected="isAllselected"
            :indeterminate="indeterminate"
            :areHeadersHidden="areHeadersHidden"
            @get-header-width-map="headerWidthMap = $event"
            @is-resizing="isResizing = $event"
            @last-vis-header="lastVisHeader = $event"
            @on-sorting="onSorting"
            @on-group="onGrouping"
            @toggle-select-all="handleSelectAll"
        >
            <template v-for="h in tableHeaders" v-slot:[`header-${h.text}`]="{ data }">
                <slot :name="`header-${h.text}`" :data="data" />
            </template>
        </table-header>
        <v-virtual-scroll
            v-if="rowsLength && !areHeadersHidden"
            ref="vVirtualScroll"
            :bench="isVertTable ? 1 : bench"
            :items="currRows"
            :height="tbodyHeight"
            :item-height="rowHeight"
            class="tbody"
            @scroll.native="scrolling"
        >
            <template v-slot:default="{ item: row, index: rowIdx }">
                <vertical-row
                    v-if="isVertTable"
                    :row="row"
                    :tableHeaders="tableHeaders"
                    :lineHeight="lineHeight"
                    :headerWidthMap="headerWidthMap"
                    :isYOverflowed="isYOverflowed"
                    @contextmenu.native.prevent="e => $emit('on-row-right-click', { e, row })"
                >
                    <template
                        v-for="h in tableHeaders"
                        v-slot:[h.text]="{ data: { cell, header, colIdx } }"
                    >
                        <slot
                            :name="`${h.text}`"
                            :data="{
                                rowData: row,
                                cell,
                                header,
                                maxWidth: cellMaxWidth(1),
                                rowIdx,
                                colIdx,
                            }"
                        >
                            <truncate-string :text="`${cell}`" :maxWidth="cellMaxWidth(1)" />
                        </slot>
                    </template>
                    <template
                        v-for="h in tableHeaders"
                        v-slot:[`vertical-header-${h.text}`]="{ data }"
                    >
                        <slot :name="`vertical-header-${h.text}`" :data="data" />
                    </template>
                </vertical-row>
                <row-group
                    v-else-if="isRowGroup(row) && !areHeadersHidden"
                    :row="row"
                    :collapsedRowGroups="collapsedRowGroups"
                    :isCollapsed="isRowGroupCollapsed(row)"
                    :boundingWidth="boundingWidth"
                    :lineHeight="lineHeight"
                    @update-collapsed-row-groups="collapsedRowGroups = $event"
                    @on-ungroup="$refs.tableHeader.handleToggleGroup(activeGroupBy)"
                >
                    <template v-if="showSelect" v-slot:row-content-prepend>
                        <row-group-checkbox
                            v-model="selectedGroupItems"
                            :row="row"
                            :tableRows="tableRows"
                            :selectedItems="selectedItems"
                            @update-selected-items="selectedItems = $event"
                        />
                    </template>
                </row-group>
                <div
                    v-else
                    class="tr"
                    :class="{
                        'tr--selected': isRowSelected(row),
                        'tr--active': $help.lodash.isEqual(activeRow, row),
                    }"
                    :style="{ lineHeight }"
                    @contextmenu.prevent="e => $emit('on-row-right-click', { e, row })"
                >
                    <div
                        v-if="!areHeadersHidden && showSelect"
                        class="td d-flex align-center justify-center"
                        :style="{
                            height: lineHeight,
                            maxWidth: activeGroupBy ? '82px' : '50px',
                            minWidth: activeGroupBy ? '82px' : '50px',
                        }"
                    >
                        <v-checkbox
                            :input-value="isRowSelected(row)"
                            dense
                            class="checkbox--scale-reduce ma-0 pa-0"
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
                    <template v-for="(h, i) in tableHeaders">
                        <!-- dependency keys to force a rerender -->
                        <div
                            v-if="!h.hidden"
                            :key="`${h.text}_${headerWidthMap[i]}_${i}`"
                            class="td"
                            :class="[
                                h.draggable ? 'cursor--grab no-userSelect' : '',
                                h.text === $typy(lastVisHeader, 'text').safeString
                                    ? `td--last-cell ${!isYOverflowed ? 'pl-3 pr-0' : 'px-3'}`
                                    : 'px-3',
                            ]"
                            :style="{
                                height: lineHeight,
                                minWidth: $help.handleAddPxUnit(headerWidthMap[i]),
                            }"
                            v-on="h.draggable ? { mousedown: e => onCellDragStart(e) } : null"
                        >
                            <slot
                                :name="h.text"
                                :data="{
                                    rowData: row,
                                    cell: row[i],
                                    header: h,
                                    maxWidth: cellMaxWidth(i),
                                    rowIdx: rowIdx,
                                    colIdx: i,
                                }"
                            >
                                <truncate-string
                                    :text="`${row[i]}`"
                                    :maxWidth="cellMaxWidth(i)"
                                    :disabled="isDragging"
                                />
                            </slot>
                        </div>
                    </template>
                    <div
                        v-if="!isYOverflowed"
                        :style="{ minWidth: `${$help.getScrollbarWidth()}px`, height: lineHeight }"
                        class="dummy-cell color border-right-table-border border-bottom-table-border"
                    />
                </div>
            </template>
        </v-virtual-scroll>
        <div v-else class="tr" :style="{ lineHeight, height: `${maxTbodyHeight}px` }">
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
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import TableHeader from './TableHeader'
import VerticalRow from './VerticalRow.vue'
import RowGroup from './RowGroup.vue'
import RowGroupCheckbox from './RowGroupCheckbox.vue'
import customDragEvt from 'mixins/customDragEvt'
export default {
    name: 'virtual-scroll-table',
    components: {
        'table-header': TableHeader,
        'vertical-row': VerticalRow,
        'row-group': RowGroup,
        'row-group-checkbox': RowGroupCheckbox,
    },
    mixins: [customDragEvt],
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
        maxHeight: { type: Number, required: true },
        itemHeight: { type: Number, required: true },
        boundingWidth: { type: Number, required: true },
        bench: { type: Number, default: 10 },
        isVertTable: { type: Boolean, default: false },
        showSelect: { type: Boolean, default: false },
        groupBy: { type: String, default: '' },
        // row being highlighted. e.g. opening ctx menu of a row
        activeRow: { type: Array, default: () => [] },
    },
    data() {
        return {
            headerWidthMap: {},
            lastVisHeader: {},
            headerStyle: {},
            isResizing: false,
            lastScrollTop: 0,
            idxOfSortingCol: -1,
            isDesc: false,
            //GroupBy feat states
            activeGroupBy: '',
            idxOfGroupCol: -1,
            collapsedRowGroups: [],
            // Select feat states
            selectedItems: [],
            selectedGroupItems: [],
        }
    },
    computed: {
        lineHeight() {
            return `${this.itemHeight}px`
        },
        visHeaders() {
            return this.tableHeaders.filter(h => !h.hidden)
        },
        rowHeight() {
            return this.isVertTable ? this.itemHeight * this.visHeaders.length : this.itemHeight
        },
        rowsHeight() {
            return this.currRows.length * this.rowHeight
        },
        maxTbodyHeight() {
            return this.maxHeight - 30 // header fixed height is 30px
        },
        tbodyHeight() {
            return this.rowsHeight >= this.maxTbodyHeight ? this.maxTbodyHeight : this.rowsHeight
        },
        isYOverflowed() {
            return this.rowsHeight > this.tbodyHeight
        },
        rowsLength() {
            return this.currRows.length
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
        currRows() {
            return this.handleFilterGroupRows(this.tableRows)
        },
        tableHeaders() {
            if (this.idxOfGroupCol === -1) return this.headers
            return this.headers.map(h =>
                this.activeGroupBy === h.text ? { ...h, hidden: true } : h
            )
        },
        isAllselected() {
            if (!this.selectedItems.length) return false
            return this.selectedItems.length === this.rowsLength
        },
        indeterminate() {
            if (!this.selectedItems.length) return false
            return !this.isAllselected && this.selectedItems.length < this.rowsLength
        },
        areHeadersHidden() {
            return this.visHeaders.length === 0
        },
    },
    watch: {
        selectedItems: {
            deep: true,
            handler(v) {
                this.$emit('item-selected', v)
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
    mounted() {
        if (this.groupBy) this.$refs.tableHeader.handleToggleGroup(this.groupBy)
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
            this.idxOfSortingCol = this.tableHeaders.findIndex(h => h.text === sortBy)
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
            let groupRows = []
            for (const [key, value] of rowMap) {
                groupRows.push({
                    groupBy: this.activeGroupBy,
                    value: key,
                    groupLength: value.length,
                })
                groupRows = [...groupRows, ...value]
            }
            return groupRows
        },

        /**
         * @param {String} activeGroupBy - header name
         */
        onGrouping(activeGroupBy) {
            this.activeGroupBy = activeGroupBy
            this.idxOfGroupCol = this.headers.findIndex(h => h.text === activeGroupBy)
            this.$emit('is-grouping', Boolean(activeGroupBy))
        },

        /**
         * @param {Object|Array} row - row to check
         * @returns {Boolean} - return whether this is a group row or not
         */
        isRowGroup(row) {
            return this.$typy(row).isObject
        },

        /**
         * @param {Object} row - row group object
         * @returns {Boolean} - return true if it is found in collapsedRowGroups data
         */
        isRowGroupCollapsed(row) {
            const targetIdx = this.collapsedRowGroups.findIndex(r =>
                this.$help.lodash.isEqual(row, r)
            )
            return targetIdx === -1 ? false : true
        },

        /**
         * @param {Array} groupRows - rows that have been grouped
         * @returns {Array} - filtered rows by collapsedRowGroups values
         */
        handleFilterGroupRows(groupRows) {
            let hiddenRowIdxs = []
            if (this.collapsedRowGroups.length) {
                for (const [i, r] of groupRows.entries()) {
                    if (this.isRowGroupCollapsed(r)) {
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
         * @param {Array} row - row array
         * @returns {Number} - returns index of row array in selectedItems
         */
        getSelectedRowIdx(row) {
            return this.selectedItems.findIndex(ele => this.$help.lodash.isEqual(ele, row))
        },

        /**
         * @param {Array} row - row array
         * @returns {Boolean} - returns true if row is found in selectedItems
         */
        isRowSelected(row) {
            return this.getSelectedRowIdx(row) === -1 ? false : true
        },

        /**
         * @param {Boolean} v - is row selected
         */
        handleSelectAll(v) {
            // don't select group row
            if (v) {
                this.selectedItems = this.tableRows.filter(row => Array.isArray(row))
                this.selectedGroupItems = this.tableRows.filter(row => !Array.isArray(row))
            } else {
                this.selectedItems = []
                this.selectedGroupItems = []
            }
        },

        onCellDragStart(e) {
            e.preventDefault()
            // Assign value to data in customDragEvt mixin
            this.isDragging = true
            this.dragTarget = e.target
        },
    },
}
</script>

<style lang="scss" scoped>
::v-deep.virtual-table {
    width: 100%;
    .tbody {
        overflow: auto;
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
                &--last-cell {
                    border-right: none;
                }
            }
            &:hover {
                .td,
                .dummy-cell {
                    background: $table-row-hover;
                }
            }
            &:active,
            &--active {
                .td,
                .dummy-cell {
                    background: #f2fcff !important;
                }
            }
            &--selected {
                .td,
                .dummy-cell {
                    background: $selected-row !important;
                }
            }
        }
    }
}
</style>
