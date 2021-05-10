<template>
    <div>
        <div ref="tableTools" class="table-tools pb-2 d-inline-flex align-center">
            <v-text-field
                v-model="searchCellKeyword"
                name="filter"
                dense
                outlined
                height="28"
                class="std filter-result mr-auto"
                placeholder="Filter result"
                hide-details
            />
            <v-menu
                allow-overflow
                transition="slide-y-transition"
                offset-y
                left
                content-class="mariadb-select-v-menu mariadb-select-v-menu--full-border"
                :close-on-content-click="false"
            >
                <template v-slot:activator="{ on, attrs, value }">
                    <v-btn
                        x-small
                        class="text-capitalize font-weight-medium arrow-toggle"
                        outlined
                        depressed
                        small
                        color="accent-dark"
                        v-bind="attrs"
                        v-on="on"
                    >
                        Columns
                        <v-icon
                            size="24"
                            color="accent-dark"
                            :class="{ 'column-list-toggle--active': value }"
                        >
                            arrow_drop_down
                        </v-icon>
                    </v-btn>
                </template>
                <v-list max-width="220px" :max-height="tableHeight - 20" class="column-list">
                    <v-list-item class="px-0" dense>
                        <v-text-field
                            v-model="searchHeaderKeyword"
                            dense
                            outlined
                            height="36"
                            class="std column-list__search"
                            placeholder="Search"
                            hide-details
                        />
                    </v-list-item>
                    <v-divider />

                    <v-list-item class="px-2" dense link>
                        <v-checkbox
                            dense
                            color="primary"
                            class="pa-0 ma-0 checkbox d-flex align-center"
                            hide-details
                            label="Select all"
                            :input-value="isAllHeaderChecked"
                            @change="toggleAllHeaders"
                        />
                    </v-list-item>
                    <v-divider />
                    <v-list-item
                        v-for="item in columnList"
                        :key="`${item.name}_${item.index}`"
                        class="px-2"
                        dense
                        link
                    >
                        <v-checkbox
                            v-model="visHeaderIndexes"
                            dense
                            color="primary"
                            class="pa-0 ma-0 checkbox d-flex align-center"
                            :value="item.index"
                            hide-details
                        >
                            <template v-slot:label>
                                <!-- TODO: show tooltip if it's truncated -->
                                <span> {{ item.name }}</span>
                            </template>
                        </v-checkbox>
                    </v-list-item>
                </v-list>
            </v-menu>
        </div>

        <virtual-scroll-table
            :benched="0"
            :headers="visibleHeaders"
            :rows="filteredRows"
            :itemHeight="30"
            :height="tableHeight"
            :width="width"
            @scroll-end="fetchMore"
        />
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
    name: 'result-data-table',
    props: {
        headers: { type: Array, require: true },
        rows: { type: Array, require: true },
        height: { type: Number, require: true },
        width: { type: Number, require: true },
    },
    data() {
        return {
            visHeaderIndexes: [],
            searchCellKeyword: '',
            tableToolsHeight: 0,
            searchHeaderKeyword: '',
        }
    },
    computed: {
        tableHeight() {
            let res = this.height - this.tableToolsHeight
            return res
        },
        tableHeaders() {
            return ['Row', ...this.headers]
        },
        rowsWithIndex() {
            return this.rows.map((row, i) => [i + 1, ...row])
        },
        columnList() {
            let list = this.$help.lodash
                .cloneDeep(this.tableHeaders)
                .map((h, i) => ({ index: i, name: h }))
            return list.filter(obj =>
                this.$help.ciStrIncludes(`${obj.name}`, this.searchHeaderKeyword)
            )
        },
        filteredRows() {
            const rows = this.rowsWithIndex.map(row =>
                row.filter((cell, i) => this.visHeaderIndexes.includes(i))
            )
            return rows.filter(row => this.$help.ciStrIncludes(`${row}`, this.searchCellKeyword))
        },
        visibleHeaders() {
            return this.tableHeaders.filter((h, i) => this.visHeaderIndexes.includes(i))
        },
        isAllHeaderChecked() {
            return this.visHeaderIndexes.length === this.tableHeaders.length
        },
    },
    mounted() {
        this.setTableToolsHeight()
        this.showAllHeaders()
    },
    methods: {
        toggleAllHeaders(v) {
            if (!v) this.visHeaderIndexes = []
            else this.showAllHeaders()
        },
        showAllHeaders() {
            this.visHeaderIndexes = this.tableHeaders.map((h, i) => i)
        },
        setTableToolsHeight() {
            if (!this.$refs.tableTools) return
            this.tableToolsHeight = this.$refs.tableTools.clientHeight
        },
        async fetchMore() {
            /* TODO: emit to parent */
        },
    },
}
</script>

<style lang="scss" scoped>
.std.filter-result {
    max-width: 250px;
}
.table-tools {
    width: 100%;
}
.column-list-toggle--active {
    transform: rotate(180deg);
    transition: 0.3s cubic-bezier(0.25, 0.8, 0.5, 1), visibility 0s;
}
.column-list {
    overflow-y: auto;
    &__search {
        ::v-deep .v-input__control {
            fieldset {
                border: none !important;
            }
        }
    }
}
::v-deep.checkbox {
    width: 100%;
    height: 36px;
    label {
        height: 36px !important;
        font-size: 0.875rem;
        color: $navigation;
        display: inline-block !important;
        white-space: nowrap !important;
        overflow: hidden !important;
        text-overflow: ellipsis !important;
        line-height: 36px;
    }
}
</style>
