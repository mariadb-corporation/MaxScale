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
            :boundingWidth="maxBoundingWidth"
            :headerStyle="headerStyle"
            :curr2dRowsLength="curr2dRowsLength"
            :showSelect="showSelect"
            :checkboxColWidth="checkboxColWidth"
            :isAllselected="isAllselected"
            :indeterminate="indeterminate"
            :areHeadersHidden="areHeadersHidden"
            :scrollBarThicknessOffset="scrollBarThicknessOffset"
            @get-header-width-map="headerWidthMap = $event"
            @is-resizing="isResizing = $event"
            @on-sorting="onSorting"
            @on-group="onGrouping"
            @toggle-select-all="handleSelectAll"
        >
            <template v-for="(_, slot) in $scopedSlots" v-slot:[slot]="props">
                <slot :name="slot" v-bind="props" />
            </template>
        </table-header>
        <v-virtual-scroll
            v-if="initialRowsLength && !areHeadersHidden"
            ref="vVirtualScroll"
            :bench="isVertTable ? 1 : bench"
            :items="currRows"
            :height="maxTbodyHeight"
            :item-height="rowHeight"
            class="tbody"
            @scroll.native="scrolling"
        >
            <template v-slot:default="{ item: row, index: rowIdx }">
                <vertical-row
                    v-if="isVertTable"
                    :row="row"
                    :rowIdx="rowIdx"
                    :tableHeaders="tableHeaders"
                    :lineHeight="lineHeight"
                    :headerWidthMap="headerWidthMap"
                    :cellContentWidthMap="cellContentWidthMap"
                    :genActivatorID="genActivatorID"
                    :isDragging="isDragging"
                    :search="search"
                    @mousedown="onCellDragStart"
                    v-on="$listeners"
                >
                    <template v-for="(_, slot) in $scopedSlots" v-slot:[slot]="props">
                        <slot :name="slot" v-bind="props" />
                    </template>
                </vertical-row>
                <row-group
                    v-else-if="isRowGroup(row) && !areHeadersHidden"
                    :collapsedRowGroups.sync="collapsedRowGroups"
                    :selectedGroupRows.sync="selectedGroupRows"
                    :selectedTblRows.sync="selectedTblRows"
                    :row="row"
                    :tableRows="tableRows"
                    :isCollapsed="isRowGroupCollapsed(row)"
                    :boundingWidth="maxBoundingWidth"
                    :lineHeight="lineHeight"
                    :showSelect="showSelect"
                    :maxWidth="maxRowGroupWidth"
                    @on-ungroup="$refs.tableHeader.handleToggleGroup(activeGroupBy)"
                />
                <horiz-row
                    v-else
                    :row="row"
                    :rowIdx="rowIdx"
                    :selectedTblRows.sync="selectedTblRows"
                    :areHeadersHidden="areHeadersHidden"
                    :tableHeaders="tableHeaders"
                    :lineHeight="lineHeight"
                    :showSelect="showSelect"
                    :checkboxColWidth="checkboxColWidth"
                    :activeRow="activeRow"
                    :genActivatorID="genActivatorID"
                    :headerWidthMap="headerWidthMap"
                    :cellContentWidthMap="cellContentWidthMap"
                    :isDragging="isDragging"
                    :search="search"
                    @mousedown="onCellDragStart"
                    v-on="$listeners"
                >
                    <template v-for="(_, slot) in $scopedSlots" v-slot:[slot]="props">
                        <slot :name="slot" v-bind="props" />
                    </template>
                </horiz-row>
            </template>
        </v-virtual-scroll>
        <div v-else class="tr" :style="{ lineHeight, height: `${maxTbodyHeight}px` }">
            <div class="td px-3 no-data-text d-flex justify-center flex-grow-1">
                {{ noDataText ? noDataText : $mxs_t('$vuetify.noDataText') }}
            </div>
        </div>
        <div v-if="isResizing" class="dragging-mask" />
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
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
@on-cell-right-click: { e: event, row:[], cell:string, activatorID:string }
@selected-rows: value:any[][]. Event is emitted when showSelect props is true
@scroll-end: Emit when table scroll to the last row
@is-grouping: boolean
*/
import TableHeader from '@wsSrc/components/common/MxsVirtualScrollTbl/TableHeader'
import VerticalRow from '@wsSrc/components/common/MxsVirtualScrollTbl/VerticalRow.vue'
import HorizRow from '@wsSrc/components/common/MxsVirtualScrollTbl/HorizRow.vue'
import RowGroup from '@wsSrc/components/common/MxsVirtualScrollTbl/RowGroup.vue'
import customDragEvt from '@share/mixins/customDragEvt'
export default {
    name: 'mxs-virtual-scroll-tbl',
    components: {
        TableHeader,
        VerticalRow,
        HorizRow,
        RowGroup,
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
        search: { type: String, default: '' }, // Text input used to highlight cell
        noDataText: { type: String, default: '' },
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
            idxOfGroupCol: -1,
            collapsedRowGroups: [],
            // Select feat states
            selectedTblRows: [],
            selectedGroupRows: [],
        }
    },
    computed: {
        scrollBarThicknessOffset() {
            return this.$helpers.getScrollbarWidth()
        },
        // minus scrollbar thickness if body is vertically overflow
        maxBoundingWidth() {
            return this.boundingWidth - (this.isYOverflowed ? this.scrollBarThicknessOffset : 0)
        },
        lineHeight() {
            return `${this.itemHeight}px`
        },
        maxRowGroupWidth() {
            let width = Object.values(this.headerWidthMap).reduce((acc, v, idx) => {
                if (idx !== this.idxOfGroupCol) acc += this.$typy(v).safeNumber
                return acc
            }, 0)
            if (this.showSelect) width += this.checkboxColWidth
            return width
        },
        checkboxColWidth() {
            return this.activeGroupBy ? 82 : 50
        },
        visHeaders() {
            return this.tableHeaders.filter(h => !h.hidden)
        },
        rowHeight() {
            return this.isVertTable ? this.itemHeight * this.visHeaders.length : this.itemHeight
        },
        rowsHeight() {
            return this.currRowsLength * this.rowHeight + this.scrollBarThicknessOffset
        },
        maxTbodyHeight() {
            return this.maxHeight - 30 // header fixed height is 30px
        },
        isYOverflowed() {
            return this.rowsHeight > this.maxTbodyHeight
        },
        // initial rows length
        initialRowsLength() {
            return this.rows.length
        },
        // indicates the number of current rows (rows after being filtered), excluding row group objects
        curr2dRowsLength() {
            return this.currRows.filter(row => !this.isRowGroup(row)).length
        },
        // indicates the number of current rows (rows after being filtered) including rows group
        currRowsLength() {
            return this.currRows.length
        },
        tableRows() {
            let rows = this.$helpers.stringifyClone(this.rows)
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
            if (!this.selectedTblRows.length) return false
            return this.selectedTblRows.length === this.initialRowsLength
        },
        indeterminate() {
            if (!this.selectedTblRows.length) return false
            return !this.isAllselected && this.selectedTblRows.length < this.initialRowsLength
        },
        areHeadersHidden() {
            return this.visHeaders.length === 0
        },
        // minus padding. i.e px-3
        cellContentWidthMap() {
            return Object.keys(this.headerWidthMap).reduce((obj, key) => {
                obj[key] = this.$typy(this.headerWidthMap[key]).safeNumber - 24
                return obj
            }, {})
        },
    },
    watch: {
        selectedTblRows: {
            deep: true,
            handler(v) {
                this.$emit('selected-rows', v)
            },
        },
        rows: {
            deep: true,
            handler(v, oV) {
                // Clear selectedTblRows once rows value changes
                if (!this.$helpers.lodash.isEqual(v, oV)) this.selectedTblRows = []
            },
        },
        isVertTable(v) {
            // clear selected items
            if (v) this.selectedTblRows = []
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
        genActivatorID: id => `activator_id-${id}`,
        //SORT FEAT
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
        // GROUP feat
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
            const header = this.headers[this.idxOfGroupCol]
            if (header.customGroup)
                rowMap = header.customGroup({
                    rows,
                    idx: this.idxOfGroupCol,
                })
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
         *  If provided row is found in collapsedRowGroups data, it's collapsed
         * @param {Object} row - row group object
         * @returns {Boolean} - return true if it is collapsed
         */
        isRowGroupCollapsed(row) {
            const targetIdx = this.collapsedRowGroups.findIndex(r =>
                this.$helpers.lodash.isEqual(row, r)
            )
            return targetIdx === -1 ? false : true
        },
        /**
         * Filter out rows that have been collapsed by collapsedRowGroups
         * @param {Array} tableRows - tableRows
         * @returns {Array} - filtered rows
         */
        handleFilterGroupRows(tableRows) {
            let hiddenRowIdxs = []
            if (this.collapsedRowGroups.length) {
                for (const [i, r] of tableRows.entries()) {
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
            return tableRows.filter((_, i) => !hiddenRowIdxs.includes(i))
        },
        // SELECT feat
        /**
         * @param {Boolean} v - is row selected
         */
        handleSelectAll(v) {
            // don't select group row
            if (v) {
                this.selectedTblRows = this.tableRows.filter(row => Array.isArray(row))
                this.selectedGroupRows = this.tableRows.filter(row => !Array.isArray(row))
            } else {
                this.selectedTblRows = []
                this.selectedGroupRows = []
            }
        },
        // DRAG feat
        onCellDragStart(e) {
            e.preventDefault()
            // Assign value to data in customDragEvt mixin
            this.isDragging = true
            this.dragTarget = e.target
        },
    },
}
</script>

<style lang="scss">
.virtual-table {
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
                background: white;
                &:first-of-type {
                    border-left: thin solid $table-border;
                }
            }
            &:hover {
                .td {
                    background: $tr-hovered-color;
                }
            }
            &:active,
            &--active,
            &--selected {
                .td {
                    background: $selected-tr-color !important;
                }
            }
        }
    }
    .no-data-text {
        font-size: 0.875rem;
    }
}
</style>
