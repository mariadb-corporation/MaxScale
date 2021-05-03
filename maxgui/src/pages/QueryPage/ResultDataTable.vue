<template>
    <div>
        <div ref="tableTools" class="table-tools">
            <v-text-field
                v-model="search_keyword"
                name="filter"
                dense
                outlined
                height="28"
                class="std filter-result pb-2"
                placeholder="Filter result"
                hide-details
            />
        </div>
        <virtual-scroll-table
            :benched="0"
            :headers="headers"
            :rows="filteredRows"
            :itemHeight="30"
            :height="tableHeight"
            :width="width"
            showRowIndex
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
            search_keyword: '',
            tableToolsHeight: 0,
        }
    },
    computed: {
        tableHeight() {
            let res = this.height - this.tableToolsHeight
            return res
        },
        filteredRows() {
            return this.rows.filter(item => `${item}`.includes(this.search_keyword))
        },
    },
    mounted() {
        this.setTableToolsHeight()
    },
    methods: {
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
    width: 200px;
}
</style>
