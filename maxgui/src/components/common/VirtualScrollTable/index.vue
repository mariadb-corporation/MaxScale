<template>
    <!-- TODO:Add resizable column feat -->
    <div class="virtual-table">
        <div class="virtual-table__header">
            <div class="tr" :style="trStyle">
                <div
                    v-for="(header, i) in tableHeaders"
                    :key="`${header.name}_${i}`"
                    :ref="`header__${i}`"
                    :style="{ ...headerStyle, minWidth: '100px' }"
                    class="th px-3"
                >
                    {{ header.name }}
                </div>
            </div>
        </div>
        <v-virtual-scroll
            v-if="rows.length"
            bench="0"
            :items="rows"
            :height="height - itemHeight"
            :item-height="itemHeight"
            @scroll.native="scrolling"
        >
            <template v-slot:default="{ item: row, index }">
                <div class="tr" :style="trStyle">
                    <div
                        v-if="showRowIndex"
                        class="td px-3"
                        :style="{ minWidth: `${cellWidthMap[0]}px` }"
                    >
                        {{ index + 1 }}
                    </div>
                    <div
                        v-for="(cell, i) in row"
                        :key="`${cell}_${i}`"
                        :style="{ minWidth: `${cellWidthMap[showRowIndex ? i + 1 : i]}px` }"
                        class="td px-3 d-inline-block text-truncate"
                    >
                        <!-- TODO: show truncated text in tooltip -->
                        {{ cell }}
                    </div>
                </div>
            </template>
        </v-virtual-scroll>
        <div v-else class="tr" :style="trStyle">
            <!-- TODO: add overflow -->
            <div class="td px-3 text-center" :style="{ width: '100%' }">
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
export default {
    name: 'virtual-scroll-table',
    props: {
        headers: { type: Array, require: true },
        rows: { type: Array, require: true },
        height: { type: Number, require: true },
        itemHeight: { type: Number, require: true },
        benched: { type: Number, require: true },
    },
    data() {
        return {
            cellWidthMap: {},
            headerStyle: {},
            showRowIndex: true,
        }
    },
    computed: {
        tableHeaders() {
            let headers = this.headers
            if (this.showRowIndex) {
                headers = [{ name: 'Row' }, ...headers]
            }
            return headers
        },
        trStyle() {
            return {
                lineHeight: `${this.itemHeight}px`,
            }
        },
    },
    mounted() {
        //TODO: Min width for header
        this.$nextTick(() => {
            this.setCellWidthMap()
        })
    },
    methods: {
        setCellWidthMap() {
            let cellWidthMap = {}
            // get width of each header then use it to set same width of corresponding cells
            for (let i = 0; i < this.tableHeaders.length; i++) {
                cellWidthMap = {
                    ...cellWidthMap,
                    [i]: this.$refs[`header__${i}`][0].clientWidth,
                }
            }
            this.cellWidthMap = cellWidthMap
        },

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
    .tr {
        display: flex;
        flex-direction: row;
        .td {
            font-size: 0.875rem;
            color: $navigation;
            border-bottom: thin solid $table-border;
            white-space: nowrap;
            &:first-of-type {
                border-left: thin solid $table-border;
            }
            &:last-of-type {
                border-right: thin solid $table-border;
            }
        }
    }

    &__header {
        overflow: hidden;
        .tr {
            box-shadow: -7px 5px 7px -7px rgb(0 0 0 / 10%);
            .th {
                font-weight: bold;
                user-select: none;
                font-size: 0.75rem;
                color: $small-text;
                background-color: $table-border;
                text-transform: uppercase;
                white-space: nowrap;
                border-bottom: none;
                &:first-child {
                    border-radius: 5px 0 0 0;
                }
                &:last-child {
                    border-radius: 0 5px 0 0;
                }
            }
        }
    }
    // Always show scrollbar to make table header and table cell align vertically
    .v-virtual-scroll {
        overflow: scroll;
    }
}
</style>
