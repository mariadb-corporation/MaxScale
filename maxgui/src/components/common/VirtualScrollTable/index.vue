<template>
    <div class="virtual-table" :class="{ 'no-userSelect': isResizing }">
        <table-header
            ref="tableHeader"
            :isVertTable="isVertTable"
            :headers="visHeaders"
            :boundingWidth="boundingWidth"
            :headerStyle="headerStyle"
            :rowsLength="tableRows.length"
            :totalGroupsLength="totalGroupsLength"
            @get-header-width-map="cellWidthMap = $event"
            @is-resizing="isResizing = $event"
            @on-sorting="onSorting"
            @on-group="onGroup"
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
                    <template v-for="(cell, i) in row">
                        <div
                            :key="`${cell}_${i}`"
                            class="tr align-center"
                            :style="{ height: lineHeight }"
                        >
                            <div
                                :key="`${cell}_${cellWidthMap[0]}_0`"
                                class="td fill-height d-flex align-center border-bottom-none px-3"
                                :style="{
                                    minWidth: $help.handleAddPxUnit(cellWidthMap[0]),
                                }"
                            >
                                <truncate-string
                                    :text="`${visHeaders[i].text}`.toUpperCase()"
                                    :maxWidth="$typy(cellWidthMap[0]).safeNumber - 24"
                                />
                            </div>
                            <div
                                :key="`${cell}_${cellWidthMap[1]}_1`"
                                class="td fill-height d-flex align-center no-border px-3"
                                :style="{
                                    minWidth: $help.handleAddPxUnit(cellWidthMap[1]),
                                }"
                            >
                                <slot
                                    :name="visHeaders[1].text"
                                    :data="{
                                        cell,
                                        header: visHeaders[1],
                                        maxWidth: cellMaxWidth(1),
                                    }"
                                >
                                    <truncate-string
                                        :text="`${cell}`"
                                        :maxWidth="cellMaxWidth(1)"
                                    />
                                </slot>
                            </div>
                        </div>
                    </template>
                </div>
                <div v-else-if="isGroupRow(row)" class="tr tr--group" :style="{ lineHeight }">
                    <div
                        class="td px-3 td-col-span"
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
                        {{ row.groupBy }} : {{ row.value }}
                        <v-btn class="ml-2" width="24" height="24" icon @click="handleUngroup">
                            <v-icon size="10" color="deep-ocean"> $vuetify.icons.close</v-icon>
                        </v-btn>
                    </div>
                </div>
                <div v-else class="tr" :style="{ lineHeight }">
                    <!-- dependency keys to force a rerender -->
                    <div
                        v-for="(cell, i) in row"
                        :key="`${cell}_${cellWidthMap[i]}_${i}`"
                        class="td px-3"
                        :style="{
                            height: lineHeight,
                            minWidth: $help.handleAddPxUnit(cellWidthMap[i]),
                        }"
                    >
                        <slot
                            :name="visHeaders[i].text"
                            :data="{ cell, header: visHeaders[i], maxWidth: cellMaxWidth(i) }"
                        >
                            <truncate-string :text="`${cell}`" :maxWidth="cellMaxWidth(i)" />
                        </slot>
                    </div>
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
    },
    data() {
        return {
            cellWidthMap: {},
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
            return this.headers.filter(h => this.activeGroupBy !== h.text)
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
            return this.$typy(this.cellWidthMap[i]).safeNumber - 24
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
        handleGroupRows(rows) {
            //TODO: Provide customGroup which is useful for grouping timestamp value by date
            let groupRows = []
            let hash = {}
            rows.forEach(row =>
                (hash[row[this.idxOfGroupCol]] || (hash[row[this.idxOfGroupCol]] = [])).push(row)
            )
            this.assignTotalGroupsLength(Object.keys(hash).length)
            Object.keys(hash).forEach(v => {
                groupRows.push({
                    groupBy: this.activeGroupBy,
                    value: v,
                    groupLength: hash[v].length,
                })
                groupRows = [
                    ...groupRows,
                    ...hash[v].map(r => r.filter((_, idx) => idx !== this.idxOfGroupCol)),
                ]
            })
            return this.handleFilterGroupRows(groupRows)
        },
        /**
         * @param {Object} payload.activeGroupBy - header name
         * @param {Number} payload.i - header index
         */
        onGroup({ header, activeGroupBy }) {
            this.activeGroupBy = activeGroupBy
            this.activeGroupByHeader = header
            this.idxOfGroupCol = this.headers.findIndex(h => h.text === activeGroupBy)
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
