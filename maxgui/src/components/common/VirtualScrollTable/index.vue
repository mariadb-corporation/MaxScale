<template>
    <div class="virtual-table" :class="{ 'no-pointerEvent no-userSelect': isResizing }">
        <table-header
            :headers="headers"
            :boundingWidth="boundingWidth"
            :headerStyle="headerStyle"
            @get-header-width-map="cellWidthMap = $event"
            @is-resizing="isResizing = $event"
        />
        <v-virtual-scroll
            v-if="rows.length && headers.length"
            :bench="benched"
            :items="rows"
            :height="height - itemHeight"
            :item-height="itemHeight"
            class="tbody"
            @scroll.native="scrolling"
        >
            <template v-slot:default="{ item: row }">
                <div class="tr" :style="{ lineHeight }">
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
            </template>
        </v-virtual-scroll>
        <div
            v-else-if="!rows.length"
            class="tr"
            :style="{ lineHeight, height: `${height - itemHeight}px` }"
        >
            <!-- TODO: add overflow and width as header width -->
            <div class="td px-3 d-flex justify-center flex-grow-1">
                No data
            </div>
        </div>
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
    },
    data() {
        return {
            cellWidthMap: {},
            headerStyle: {},
            isResizing: false,
        }
    },
    computed: {
        lineHeight() {
            return `${this.itemHeight}px`
        },
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
            flex-direction: row;
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
    }
}
</style>
