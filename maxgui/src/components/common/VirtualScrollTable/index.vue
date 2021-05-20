<template>
    <div class="virtual-table" :class="{ 'no-userSelect': isResizing }">
        <table-header
            :isVertTable="isVertTable"
            :headers="headers"
            :boundingWidth="boundingWidth"
            :headerStyle="headerStyle"
            @get-header-width-map="cellWidthMap = $event"
            @is-resizing="isResizing = $event"
        />
        <v-virtual-scroll
            v-if="rows.length && headers.length"
            ref="vVirtualScroll"
            :bench="isVertTable ? 1 : benched"
            :items="rows"
            :height="tbodyHeight"
            :item-height="rowHeight"
            class="tbody"
            @scroll.native="scrolling"
        >
            <template v-slot:default="{ item: row }">
                <div v-if="!isVertTable" class="tr" :style="{ lineHeight }">
                    <!-- dependency keys to force a rerender -->
                    <div
                        v-for="(cell, i) in row"
                        :key="`${cell}_${cellWidthMap[i]}_${i}`"
                        class="td px-3"
                        :style="{
                            height: lineHeight,
                            minWidth: `${cellWidthMap[i]}px`,
                        }"
                    >
                        <truncate-string :text="`${cell}`" :maxWidth="cellWidthMap[i] - 24" />
                    </div>
                </div>
                <div v-else class="tr-vertical-group d-flex flex-column">
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
                                    minWidth: `${cellWidthMap[0]}px`,
                                }"
                            >
                                <truncate-string
                                    :text="`${headers[i]}`.toUpperCase()"
                                    :maxWidth="cellWidthMap[0] - 24"
                                />
                            </div>
                            <div
                                :key="`${cell}_${cellWidthMap[1]}_1`"
                                class="td fill-height d-flex align-center no-border px-3"
                                :style="{
                                    minWidth: `${cellWidthMap[1]}px`,
                                }"
                            >
                                <truncate-string
                                    :text="`${cell}`"
                                    :maxWidth="cellWidthMap[1] - 24"
                                />
                            </div>
                        </div>
                    </template>
                </div>
            </template>
        </v-virtual-scroll>
        <div
            v-else-if="!rows.length"
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
 * Change Date: 2025-03-24
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
        headers: { type: Array, require: true },
        rows: { type: Array, require: true },
        height: { type: Number, require: true },
        itemHeight: { type: Number, require: true },
        boundingWidth: { type: Number, require: true },
        benched: { type: Number, require: true },
        isVertTable: { type: Boolean, default: false },
    },
    data() {
        return {
            cellWidthMap: {},
            headerStyle: {},
            isResizing: false,
            lastScrollTop: 0,
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
    },
    activated() {
        /**
         * activated hook is triggered when this component is placed
         * as a children component or nested component of keep-alive.
         * For some reason, the last scrollTop position isn't preserved in
         * v-virtual-scroll component. This is a workaround to manually
         * scroll the content to lastScrollTop value
         */
        this.$refs.vVirtualScroll.$el.scrollTop = 1 // in case lastScrollTop === 0
        this.$refs.vVirtualScroll.$el.scrollTop = this.lastScrollTop
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
