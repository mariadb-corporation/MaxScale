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
                :placeholder="$t('filterResult')"
                hide-details
            />
            <result-export :rows="filteredRows_wo_idx" :headers="visHeaders_wo_idx" />
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
                        class="text-capitalize font-weight-medium"
                        outlined
                        depressed
                        color="accent-dark"
                        v-bind="attrs"
                        v-on="on"
                    >
                        {{ $t('columns') }}
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
                            :placeholder="$t('search')"
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
                            :label="$t('selectAll')"
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
                                <truncate-string :text="item.name" />
                            </template>
                        </v-checkbox>
                    </v-list-item>
                </v-list>
            </v-menu>

            <v-tooltip
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
            >
                <template v-slot:activator="{ on }">
                    <v-btn
                        x-small
                        class="ml-2 pa-1"
                        outlined
                        depressed
                        color="accent-dark"
                        v-on="on"
                        @click="isVertTable = !isVertTable"
                    >
                        <v-icon
                            size="14"
                            color="accent-dark"
                            :class="{ 'rotate-icon__vert': !isVertTable }"
                        >
                            rotate_90_degrees_ccw
                        </v-icon>
                    </v-btn>
                </template>
                <span>{{ $t(isVertTable ? 'switchToHorizTable' : 'switchToVertTable') }}</span>
            </v-tooltip>
        </div>
        <!-- Keep it in memory, negative height crashes v-virtual-scroll -->
        <keep-alive>
            <virtual-scroll-table
                v-if="tableHeight > 0"
                :benched="0"
                :headers="visibleHeaders"
                :rows="filteredRows"
                :itemHeight="30"
                :height="tableHeight"
                :boundingWidth="width"
                :isVertTable="isVertTable"
            />
        </keep-alive>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import ResultExport from './ResultExport'
export default {
    name: 'result-data-table',
    components: {
        'result-export': ResultExport,
    },
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
            isVertTable: false,
        }
    },
    computed: {
        tableHeight() {
            let res = this.height - this.tableToolsHeight
            return res
        },
        tableHeaders() {
            const headers = this.headers.length ? ['#', ...this.headers] : []
            return headers
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
        filteredRows_wo_idx() {
            return this.filteredRows.map(row => row.filter((cell, i) => i !== 0))
        },
        visHeaders_wo_idx() {
            return this.visibleHeaders.filter(header => header !== '#')
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
    activated() {
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
.rotate-icon__vert {
    transform: rotate(90deg);
}
</style>
