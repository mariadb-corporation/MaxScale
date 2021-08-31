<template>
    <div>
        <div ref="tableTools" class="table-tools pb-2 d-inline-flex align-center">
            <v-text-field
                v-model="filterKeyword"
                name="filter"
                dense
                outlined
                height="28"
                class="std filter-result mr-2"
                :placeholder="$t('filterResult')"
                hide-details
            />
            <column-list
                v-model="filterHeaderIdxs"
                :label="$t('filterBy')"
                :cols="tableHeaders"
                :maxHeight="tableHeight - 20"
            />
            <v-spacer />
            <result-export :rows="filteredRows_wo_idx" :headers="visHeaders_wo_idx" />
            <column-list
                v-model="visHeaderIdxs"
                :label="$t('columns')"
                :cols="tableHeaders"
                :maxHeight="tableHeight - 20"
            />
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
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import ResultExport from './ResultExport'
import ColumnList from './ColumnList.vue'
export default {
    name: 'result-data-table',
    components: {
        'result-export': ResultExport,
        'column-list': ColumnList,
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
        width: { type: Number, required: true },
    },
    data() {
        return {
            filterHeaderIdxs: [],
            visHeaderIdxs: [],
            filterKeyword: '',
            tableToolsHeight: 0,
            isVertTable: false,
        }
    },
    computed: {
        tableHeight() {
            let res = this.height - this.tableToolsHeight
            return res
        },
        tableHeaders() {
            const headers = this.headers.length
                ? [{ text: '#', width: 'max-content' }, ...this.headers]
                : []
            return headers
        },
        rowsWithIndex() {
            return this.rows.map((row, i) => [i + 1, ...row])
        },
        filteredRows_wo_idx() {
            return this.filteredRows.map(row => row.filter((cell, i) => i !== 0))
        },
        visHeaders_wo_idx() {
            return this.visibleHeaders.filter(header => header.text !== '#')
        },
        filteredRows() {
            const rows = this.rowsWithIndex.map(row =>
                row.filter((cell, i) => this.visHeaderIdxs.includes(i))
            )
            return rows.filter(row => {
                let match = false
                for (const [i, cell] of row.entries()) {
                    if (
                        (this.filterHeaderIdxs.includes(i) || !this.filterHeaderIdxs.length) &&
                        this.$help.ciStrIncludes(`${cell}`, this.filterKeyword)
                    ) {
                        match = true
                        break
                    }
                }
                return match
            })
        },
        visibleHeaders() {
            return this.tableHeaders.filter((h, i) => this.visHeaderIdxs.includes(i))
        },
    },
    activated() {
        this.setTableToolsHeight()
    },
    methods: {
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
.rotate-icon__vert {
    transform: rotate(90deg);
}
</style>
