<template>
    <div>
        <!-- TODO: add filter input (recalculate height of table after adding it)
            Add resizable column feat
         -->
        <data-table
            tableClass="data-table-full--max-width-columns"
            :search="search_keyword"
            :headers="headers"
            :data="rows"
            showAll
            dense
            fixed-header
            :height="height"
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
        height: { type: String, require: true },
    },
    data() {
        return {
            search_keyword: '',
        }
    },
}
</script>
