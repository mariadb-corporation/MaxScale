<template>
    <div
        class="mxs-virtual-table"
        :class="{ 'no-userSelect': isResizing }"
        :style="{ width: `${maxWidth}px`, cursor: isResizing ? 'col-resize' : '' }"
    >
        <div class="mxs-virtual-table__header-wrapper d-flex relative">
            <table-headers
                :headers="tableHeaders"
                class="relative"
                :boundingWidth="headersWidth"
                :scrollBarThickness="scrollBarThickness"
                :style="{
                    left: headerScrollLeft,
                    height: $helpers.handleAddPxUnit(headerHeight),
                }"
                :showOrderNumber="showOrderNumberHeader"
                :rowCount="rowsLength"
                @header-width-map="headerWidthMap = $event"
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
            <template v-slot:cell="{ data }">
                <!-- TODO: Add Cell component -->
                <div
                    class="td fill-height text-truncate px-3 mxs-color-helper text-navigation border-bottom-table-border border-right-table-border"
                    :class="{
                        'border-left-table-border': showOrderNumberHeader
                            ? data.isOrderNumberCol
                            : data.colIdx === 0,
                    }"
                >
                    {{ data.value }}
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
    components: {
        TableHeaders,
    },
    props: {
        headers: {
            type: Array,
            validator: arr => {
                if (!arr.length) return true
                return arr.filter(item => 'text' in item).length === arr.length
            },
            required: true,
        },
        data: { type: Array, required: true },
        maxHeight: { type: Number, required: true },
        maxWidth: { type: Number, required: true },
        headerHeight: { type: Number, default: 30 },
        rowHeight: { type: Number, default: 30 },
        showOrderNumberHeader: { type: Boolean, default: false },
        autoId: { type: Boolean, default: false }, // Enable it for CRUD operations
    },
    data() {
        return {
            orderNumberHeaderWidth: 0,
            headerWidthMap: {},
            headerScrollLeft: 0,
            isResizing: false,
            collection: [],
        }
    },
    computed: {
        tableHeaders() {
            if (this.autoId)
                // add an uuid header first
                return [{ text: 'uuid', hidden: true }, ...this.headers]
            return this.headers
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
        rowIds() {
            return Array.from({ length: this.data.length }, () => this.$helpers.uuidv1())
        },
        cells() {
            if (this.autoId) {
                let cells = []
                this.data.forEach((row, index) => {
                    // Place uuid cell first
                    cells.push(this.rowIds[index], ...row)
                })
                return cells
            }
            return this.data.flat()
        },
        colLeftPosMap() {
            let left = this.showOrderNumberHeader ? this.orderNumberHeaderWidth : 0
            return Object.values(this.headerWidthMap).reduce((res, width, i) => {
                if (!this.isHeaderHidden(i)) {
                    res[i] = left
                    left += width
                }
                return res
            }, {})
        },
    },
    watch: {
        colLeftPosMap: {
            immediate: true,
            deep: true,
            handler(v, oV) {
                if (
                    !this.$helpers.lodash.isEqual(v, oV) &&
                    !this.$typy(this.colLeftPosMap).isEmptyObject
                )
                    this.computeCollection()
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
            return this.$typy(this.tableHeaders, `[${i}].hidden`).safeBoolean
        },
        computeCollection() {
            const data = {
                tableHeaders: this.tableHeaders,
                cells: this.cells,
                autoId: this.autoId,
                showOrderNumberHeader: this.showOrderNumberHeader,
                rowHeight: this.rowHeight,
                orderNumberHeaderWidth: this.orderNumberHeaderWidth,
                headerWidthMap: this.headerWidthMap,
                colLeftPosMap: this.colLeftPosMap,
            }
            this.worker.postMessage({ action: 'compute-collection', data })
            this.worker.onmessage = event => {
                this.collection = event.data
            }
        },
    },
}
</script>
<style lang="scss">
.mxs-virtual-table {
    width: 100%;
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
        .td {
            font-size: 0.875rem;
        }
    }
}
</style>
