<template>
    <div
        class="mxs-virtual-table"
        :class="{ 'no-userSelect': isResizing }"
        :style="{ cursor: isResizing ? 'col-resize' : '' }"
    >
        <table-header
            ref="tableHeader"
            :isVertTable="isVertTable"
            :items="tableHeaders"
            :boundingWidth="maxBoundingWidth"
            :headerStyle="headerStyle"
            :rowCount="rowCount"
            :showSelect="showSelect"
            :checkboxColWidth="checkboxColWidth"
            :isAllSelected="isAllSelected"
            :indeterminate="indeterminate"
            :areHeadersHidden="areHeadersHidden"
            :scrollBarThickness="scrollBarThickness"
            :singleSelect="singleSelect"
            :showRowCount="showRowCount"
            :sortOptions.sync="sortOptions"
            @header-widths="headerWidths = $event"
            @is-resizing="isResizing = $event"
            @on-group="onGrouping"
            @toggle-select-all="handleSelectAll"
        >
            <template v-for="(_, slot) in $scopedSlots" v-slot:[slot]="props">
                <slot :name="slot" v-bind="props" />
            </template>
        </table-header>
        <v-virtual-scroll
            v-if="dataCount && !areHeadersHidden"
            ref="vVirtualScroll"
            :bench="bench"
            :items="rows"
            :height="(isYOverflowed ? maxTbodyHeight : rowsHeight) + scrollBarThickness"
            :max-height="maxTbodyHeight"
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
                    :colWidths="headerWidths"
                    :cellContentWidths="cellContentWidths"
                    :genActivatorID="genActivatorID"
                    :isDragging="isDragging"
                    :search="search"
                    @mousedown="onCellDragStart"
                    @click.native="$emit('row-click', row)"
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
                    :tableData="tableData"
                    :isCollapsed="isRowGroupCollapsed(row)"
                    :boundingWidth="maxBoundingWidth"
                    :lineHeight="lineHeight"
                    :showSelect="showSelect"
                    :maxWidth="maxRowGroupWidth"
                    @on-ungroup="$refs.tableHeader.handleToggleGroup(activeGroupBy)"
                    @click.native="$emit('row-click', row)"
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
                    :colWidths="headerWidths"
                    :cellContentWidths="cellContentWidths"
                    :isDragging="isDragging"
                    :search="search"
                    :singleSelect="singleSelect"
                    @mousedown="onCellDragStart"
                    @click.native="$emit('row-click', row)"
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
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
 * Emits:
 * - on-cell-right-click({ e: event, row:[], cell:string, activatorID:string })
 * - scroll-end()
 * - is-grouping(boolean)
 * - row-click(rowData)
 * - current-rows(array): current rows
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
        data: { type: Array, required: true },
        maxHeight: { type: Number, required: true },
        itemHeight: { type: Number, required: true },
        boundingWidth: { type: Number, required: true },
        bench: { type: Number, default: 1 },
        isVertTable: { type: Boolean, default: false },
        showSelect: { type: Boolean, default: false },
        singleSelect: { type: Boolean, default: false },
        groupBy: { type: String, default: '' },
        // row being highlighted. e.g. opening ctx menu of a row
        activeRow: { type: Array, default: () => [] },
        search: { type: String, default: '' }, // Text input used to highlight cell
        searchBy: { type: Array, default: () => [] },
        noDataText: { type: String, default: '' },
        selectedItems: { type: Array, default: () => [] }, //sync
        showRowCount: { type: Boolean, default: true },
    },
    data() {
        return {
            headerWidths: [],
            headerStyle: {},
            isResizing: false,
            lastScrollTop: 0,
            sortOptions: { sortBy: '', sortDesc: false },
            //GroupBy feat states
            activeGroupBy: '',
            idxOfGroupCol: -1,
            collapsedRowGroups: [],
            // Select feat states
            selectedGroupRows: [],
        }
    },
    computed: {
        scrollBarThickness() {
            return this.$helpers.getScrollbarWidth()
        },
        // minus scrollbar thickness if body is vertically overflow
        maxBoundingWidth() {
            return this.boundingWidth - (this.isYOverflowed ? this.scrollBarThickness : 0)
        },
        lineHeight() {
            return `${this.itemHeight}px`
        },
        maxRowGroupWidth() {
            let width = this.headerWidths.reduce((acc, v, idx) => {
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
            return this.rows.length * this.rowHeight + this.scrollBarThickness
        },
        maxTbodyHeight() {
            return this.maxHeight - 30 // header fixed height is 30px
        },
        isYOverflowed() {
            return this.rowsHeight > this.maxTbodyHeight
        },
        dataCount() {
            return this.data.length
        },
        // indicates the number of filtered rows excluding row group objects
        rowCount() {
            return this.rows.filter(row => !this.isRowGroup(row)).length
        },
        idxOfSortingCol() {
            return this.headerNamesIndexesMap[this.sortOptions.sortBy]
        },
        tableData() {
            let data = this.$helpers.lodash.cloneDeep(this.data)
            if (this.idxOfSortingCol >= 0) this.handleSort(data)
            if (this.idxOfGroupCol !== -1 && !this.isVertTable) data = this.handleGroupData(data)
            return data
        },
        rows() {
            return this.filterData(this.tableData)
        },
        tableHeaders() {
            if (this.idxOfGroupCol === -1) return this.headers
            return this.headers.map(h =>
                this.activeGroupBy === h.text ? { ...h, hidden: true } : h
            )
        },
        headerNamesIndexesMap() {
            return this.tableHeaders.reduce((map, h, i) => {
                map[h.text] = i
                return map
            }, {})
        },
        isAllSelected() {
            if (!this.selectedTblRows.length) return false
            return this.selectedTblRows.length === this.dataCount
        },
        indeterminate() {
            if (!this.selectedTblRows.length) return false
            return !this.isAllSelected && this.selectedTblRows.length < this.dataCount
        },
        areHeadersHidden() {
            return this.visHeaders.length === 0
        },
        // minus padding. i.e px-3
        cellContentWidths() {
            return this.headerWidths.map(w => w - 24)
        },
        selectedTblRows: {
            get() {
                return this.selectedItems
            },
            set(v) {
                this.$emit('update:selectedItems', v)
            },
        },
    },
    watch: {
        data: {
            deep: true,
            handler(v, oV) {
                /**
                 * Clear selectedTblRows once data quantity changes.
                 * e.g. when deleting a row
                 */
                if (!(v.length <= oV.length)) this.selectedTblRows = []
            },
        },
        isVertTable(v) {
            // clear selected items
            if (v) this.selectedTblRows = []
        },
        rows: {
            deep: true,
            handler(v) {
                this.$emit('current-rows', v)
            },
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
         * @param {Array} data - 2d array to be sorted
         */
        handleSort(data) {
            data.sort((a, b) => {
                if (this.sortOptions.sortDesc)
                    return b[this.idxOfSortingCol] < a[this.idxOfSortingCol] ? -1 : 1
                else return a[this.idxOfSortingCol] < b[this.idxOfSortingCol] ? -1 : 1
            })
        },
        // GROUP feat
        /** This groups 2d array with same value at provided index to a Map
         * @param {Array} payload.data - 2d array to be grouped into a Map
         * @param {Number} payload.idx - col index of the inner array
         * @returns {Map} - returns map with value as key and value is a matrix (2d array)
         */
        groupValues({ data, idx }) {
            let map = new Map()
            data.forEach(row => {
                const key = row[idx]
                let matrix = map.get(key) || [] // assign an empty arr if not found
                matrix.push(row)
                map.set(key, matrix)
            })
            return map
        },
        handleGroupData(data) {
            let rowMap = this.groupValues({ data, idx: this.idxOfGroupCol })
            const header = this.headers[this.idxOfGroupCol]
            if (header.customGroup)
                rowMap = header.customGroup({
                    rows: data,
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
        // SELECT feat
        /**
         * @param {Boolean} v - is row selected
         */
        handleSelectAll(v) {
            // don't select group row
            if (v) {
                this.selectedTblRows = this.tableData.filter(row => Array.isArray(row))
                this.selectedGroupRows = this.tableData.filter(row => !Array.isArray(row))
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
        //TODO: Move below methods to worker
        /**
         * Filter row by `search` keyword and `searchBy`
         * @param {Array.<Array>} row
         * @returns {boolean}
         */
        rowFilter(row) {
            if (!this.search) return true
            return row.some((cell, colIdx) => {
                const header = this.$typy(this.headers[colIdx]).safeObjectOrEmpty
                return (
                    (this.searchBy.includes(header.text) || !this.searchBy.length) &&
                    this.filter({ header, value: cell })
                )
            })
        },
        filter({ header, value }) {
            // use custom filter if it's provided
            if (header.filter) return header.filter(value, this.search)
            return this.$helpers.ciStrIncludes(`${value}`, this.search)
        },
        /**
         * Filter for row group
         * @param {Array.<Array>} param.data
         * @param {object} param.rowGroup
         * @param {number} param.rowIdx
         * @returns {boolean}
         */
        rowGroupFilter({ data, rowGroup, rowIdx }) {
            return Array(rowGroup.groupLength)
                .fill()
                .map((_, n) => data[n + rowIdx + 1])
                .some(row => this.rowFilter(row))
        },
        filterData(data) {
            let collapsedRowIndices = []
            return data.filter((row, rowIdx) => {
                const isRowGroup = this.isRowGroup(row)
                if (isRowGroup) {
                    // get indexes of collapsed rows
                    if (this.isRowGroupCollapsed(row))
                        collapsedRowIndices = [
                            ...collapsedRowIndices,
                            ...Array(row.groupLength)
                                .fill()
                                .map((_, n) => n + rowIdx + 1),
                        ]
                }
                if (collapsedRowIndices.includes(rowIdx)) return false
                if (isRowGroup) return this.rowGroupFilter({ data, rowGroup: row, rowIdx })
                return this.rowFilter(row)
            })
        },
    },
}
</script>

<style lang="scss">
.mxs-virtual-table {
    width: 100%;
    .tbody {
        overflow: auto;
        .tr {
            display: flex;
            cursor: pointer;
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
