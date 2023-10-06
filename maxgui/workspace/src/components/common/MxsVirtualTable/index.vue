<template>
    <div
        class="mxs-virtual-table"
        :class="{ 'no-userSelect': isResizing }"
        :style="{ width: `${maxWidth}px`, cursor: isResizing ? 'col-resize' : '' }"
    >
        <div class="mxs-virtual-table__header-wrapper d-flex relative">
            <table-headers
                :headers="headers"
                class="relative"
                :boundingWidth="headersWidth"
                :scrollBarThickness="scrollBarThickness"
                :style="{
                    left: headerScrollLeft,
                    height: $helpers.handleAddPxUnit(headerHeight),
                }"
                :showOrderNumber="showOrderNumberHeader"
                :rowCount="rowsLength"
                :sortOptions.sync="sortOptions"
                @header-widths="headerWidths = $event"
                @order-number-header-width="orderNumberHeaderWidth = $event"
                @is-resizing="isResizing = $event"
            >
                <template v-for="(_, slot) in $scopedSlots" v-slot:[slot]="props">
                    <slot :name="slot" v-bind="props" />
                </template>
            </table-headers>
            <div
                :style="{ minWidth: `${scrollBarThickness}px` }"
                class="d-inline-block fixed-padding"
            />
        </div>
        <VirtualCollection
            v-if="collection.length"
            class="mxs-virtual-table__body"
            :cellSizeAndPositionGetter="cellSizeAndPositionGetter"
            :collection="collection"
            :height="isYOverflowed ? maxBodyHeight : rowsHeight + scrollBarThickness"
            :width="maxWidth"
            @scroll.native="scrolling"
        >
            <template
                v-slot:cell="{ data : { value, isOrderNumberCell = false, rowIdx, colIdx, width, }}"
            >
                <div
                    class="table-cell d-flex align-center fill-height mxs-color-helper text-navigation border-bottom-table-border border-right-table-border"
                    :class="{
                        'border-left-table-border': isOrderNumberCell || colIdx === 0,
                        'px-3': isOrderNumberCell,
                    }"
                    data-test="table-cell"
                >
                    <template v-if="isOrderNumberCell"> {{ value }} </template>
                    <slot
                        v-else
                        :name="getHeaderName(colIdx)"
                        :data="{ rowIdx, colIdx, value, width }"
                    >
                        <mxs-truncate-str
                            v-mxs-highlighter="{
                                keyword: searchBy.includes(getHeaderName(colIdx)) ? search : '',
                                txt: value,
                            }"
                            class="px-3"
                            :tooltipItem="{ txt: value }"
                            :maxWidth="width"
                            :debounce="400"
                        />
                    </slot>
                </div>
            </template>
        </VirtualCollection>
    </div>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import TableHeaders from '@wsSrc/components/common/MxsVirtualTable/TableHeaders'
import worker from 'worker-loader!./worker.js'
export default {
    name: 'mxs-virtual-table',
    components: { TableHeaders },
    props: {
        headers: {
            type: Array,
            validator: arr => {
                if (!arr.length) return true
                return arr.filter(item => 'text' in item).length === arr.length
            },
            required: true,
        },
        data: { type: Array, required: true }, // 2d array
        maxHeight: { type: Number, required: true },
        maxWidth: { type: Number, required: true },
        headerHeight: { type: Number, default: 30 },
        rowHeight: { type: Number, default: 30 },
        showOrderNumberHeader: { type: Boolean, default: false },
        search: { type: String, default: '' }, // keyword for filtering items
        searchBy: { type: Array, default: () => [] }, // included header names for filtering
    },
    data() {
        return {
            orderNumberHeaderWidth: 0,
            headerWidths: [],
            headerScrollLeft: 0,
            isResizing: false,
            collection: [],
            sortOptions: { sortBy: '', sortDesc: false },
        }
    },
    computed: {
        headerNamesIndexesMap() {
            return this.headers.reduce((map, h, i) => {
                map[h.text] = i
                return map
            }, {})
        },
        rowsLength() {
            return this.data.length
        },
        scrollBarThickness() {
            return this.$helpers.getScrollbarWidth()
        },
        rowsHeight() {
            return this.rowsLength * this.rowHeight
        },
        isYOverflowed() {
            return this.rowsHeight > this.maxBodyHeight
        },
        headersWidth() {
            return this.maxWidth - (this.isYOverflowed ? this.scrollBarThickness : 0)
        },
        maxBodyHeight() {
            return this.maxHeight - this.headerHeight
        },
        colLeftPosMap() {
            let left = this.showOrderNumberHeader ? this.orderNumberHeaderWidth : 0
            return this.headerWidths.reduce((res, width, i) => {
                if (!this.isHeaderHidden(i)) {
                    res[i] = left
                    left += width
                }
                return res
            }, {})
        },
    },
    watch: {
        data: {
            deep: true,
            handler(v, oV) {
                if (!this.$helpers.lodash.isEqual(v, oV) && v.length) this.computeCollection()
            },
        },
        headerWidths: {
            immediate: true,
            deep: true,
            handler(v, oV) {
                if (!this.$helpers.lodash.isEqual(v, oV) && v.length) this.computeCollection()
            },
        },
        search() {
            this.computeCollection()
        },
        searchBy: {
            deep: true,
            handler() {
                this.computeCollection()
            },
        },
        sortOptions: {
            deep: true,
            handler() {
                this.sortCollection()
            },
        },
    },
    created() {
        this.worker = new worker()
    },
    beforeDestroy() {
        this.worker.terminate()
    },
    methods: {
        cellSizeAndPositionGetter(item) {
            const { width, height, x, y } = item
            return { width, height, x, y }
        },
        scrolling(event) {
            const ele = event.currentTarget || event.target
            //Scroll header
            this.headerScrollLeft = `-${ele.scrollLeft}px`
        },
        isHeaderHidden(i) {
            return this.$typy(this.headers, `[${i}].hidden`).safeBoolean
        },
        getHeaderName(i) {
            return this.headers[i].text
        },
        computeCollection() {
            this.worker.postMessage({
                action: 'compute',
                data: {
                    headers: this.headers,
                    rows: this.data,
                    showOrderNumberCell: this.showOrderNumberHeader,
                    rowHeight: this.rowHeight,
                    colWidths: this.headerWidths,
                    orderNumberCellWidth: this.orderNumberHeaderWidth,
                    colLeftPosMap: this.colLeftPosMap,
                    search: this.search,
                    searchBy: this.searchBy,
                },
            })
            this.worker.onmessage = e => (this.collection = e.data)
        },
        sortCollection() {
            if (this.sortOptions.sortBy < 0) this.computeCollection()
            else {
                this.worker.postMessage({
                    action: 'sort',
                    data: {
                        headerNamesIndexesMap: this.headerNamesIndexesMap,
                        rowHeight: this.rowHeight,
                        collection: this.collection,
                        sortOptions: this.sortOptions,
                        showOrderNumberCell: this.showOrderNumberHeader,
                    },
                })
                this.worker.onmessage = e => (this.collection = e.data)
            }
        },
    },
}
</script>
<style lang="scss" scoped>
.mxs-virtual-table {
    width: 100%;
    font-size: 0.875rem;
    overflow: hidden;
    &__header-wrapper {
        .fixed-padding {
            z-index: 2;
            background-color: $table-border;
            height: 30px;
            position: absolute;
            border-radius: 0 5px 0 0;
            right: 0;
        }
    }
    &__body {
        overflow: auto !important;
    }
}
</style>
