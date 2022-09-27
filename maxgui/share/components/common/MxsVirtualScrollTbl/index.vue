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
            :isAllselected="isAllselected"
            :indeterminate="indeterminate"
            :areHeadersHidden="areHeadersHidden"
            :scrollBarThicknessOffset="scrollBarThicknessOffset"
            @get-header-width-map="headerWidthMap = $event"
            @is-resizing="isResizing = $event"
            @last-vis-header="lastVisHeader = $event"
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
                    @on-ungroup="$refs.tableHeader.handleToggleGroup(activeGroupBy)"
                />
                <div
                    v-else
                    class="tr"
                    :class="{
                        'tr--selected': isRowSelected(row),
                        'tr--active': $helpers.lodash.isEqual(activeRow, row),
                    }"
                    :style="{ lineHeight }"
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
                            class="v-checkbox--scale-reduce ma-0 pa-0"
                            primary
                            hide-details
                            @change="
                                val =>
                                    val
                                        ? selectedTblRows.push(row)
                                        : selectedTblRows.splice(getSelectedRowIdx(row), 1)
                            "
                        />
                    </div>
                    <template v-for="(h, colIdx) in tableHeaders">
                        <!-- dependency keys to force a rerender -->
                        <div
                            v-if="!h.hidden"
                            :id="genActivatorID(`${rowIdx}-${colIdx}`)"
                            :key="`${h.text}_${headerWidthMap[colIdx]}_${colIdx}`"
                            class="td px-3"
                            :class="{
                                'cursor--grab no-userSelect': draggableCell && h.draggable,
                                'td--last-cell': h.text === $typy(lastVisHeader, 'text').safeString,
                            }"
                            :style="{
                                height: lineHeight,
                                minWidth: $helpers.handleAddPxUnit(headerWidthMap[colIdx]),
                            }"
                            v-on="
                                draggableCell && h.draggable
                                    ? { mousedown: e => onCellDragStart(e) }
                                    : null
                            "
                            @contextmenu.prevent="
                                e =>
                                    $emit('on-cell-right-click', {
                                        e,
                                        row,
                                        cell: row[colIdx],
                                        activatorID: genActivatorID(`${rowIdx}-${colIdx}`),
                                    })
                            "
                        >
                            <!-- cell slot -->
                            <slot
                                :name="h.text"
                                :data="{
                                    rowData: row,
                                    cell: row[colIdx],
                                    header: h,
                                    maxWidth: $typy(cellContentWidthMap[colIdx]).safeNumber,
                                    rowIdx: rowIdx,
                                    colIdx,
                                    activatorID: genActivatorID(`${rowIdx}-${colIdx}`),
                                }"
                            >
                                <mxs-truncate-str
                                    :tooltipItem="{
                                        txt: `${row[colIdx]}`,
                                        activatorID: genActivatorID(`${rowIdx}-${colIdx}`),
                                    }"
                                />
                            </slot>
                        </div>
                    </template>
                </div>
            </template>
        </v-virtual-scroll>
        <div v-else class="tr" :style="{ lineHeight, height: `${maxTbodyHeight}px` }">
            <div class="td px-3 d-flex justify-center flex-grow-1">
                {{ $mxs_t('$vuetify.noDataText') }}
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
 * Change Date: 2026-09-06
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
@on-cell-right-click: { e: event, row:[], cell:string, activatorID:string }
@selected-rows: value:any[][]. Event is emitted when showSelect props is true
@scroll-end: Emit when table scroll to the last row
Emit when the header has groupable and hasCustomGroup keys.
@custom-group: data:{ rows:any[][], idx:number, header:object}, callback():Map
@is-grouping: boolean
*/
import TableHeader from './TableHeader'
import VerticalRow from './VerticalRow.vue'
import RowGroup from './RowGroup.vue'
import customDragEvt from '@share/mixins/customDragEvt'
export default {
    name: 'mxs-virtual-scroll-tbl',
    components: {
        TableHeader,
        VerticalRow,
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
        draggableCell: { type: Boolean, default: true },
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
        // indicates the number of current rows (rows after being filtered), excluding rows group
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
                this.$helpers.lodash.isEqual(row, r)
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
         * @returns {Number} - returns index of row array in selectedTblRows
         */
        getSelectedRowIdx(row) {
            return this.selectedTblRows.findIndex(ele => this.$helpers.lodash.isEqual(ele, row))
        },

        /**
         * @param {Array} row - row array
         * @returns {Boolean} - returns true if row is found in selectedTblRows
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
                this.selectedTblRows = this.tableRows.filter(row => Array.isArray(row))
                this.selectedGroupRows = this.tableRows.filter(row => !Array.isArray(row))
            } else {
                this.selectedTblRows = []
                this.selectedGroupRows = []
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
                &--last-cell {
                    border-right: none;
                }
            }
            &:hover {
                .td {
                    background: $tr-hovered-color;
                }
            }
            &:active,
            &--active {
                .td {
                    background: #f2fcff !important;
                }
            }
            &--selected {
                .td {
                    background: $selected-tr-color !important;
                }
            }
        }
    }
}
</style>
