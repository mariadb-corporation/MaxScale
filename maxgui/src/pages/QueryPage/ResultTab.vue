<template>
    <div class="fill-height">
        <div v-if="isMounted" ref="header" class="pb-2 result-header">
            <span>
                Click Run button to see query results
            </span>
        </div>
        <div v-else class="result-table-wrapper">
            <div ref="header" class="pb-2 result-header-nav">
                <!-- TODO: Show query Id and query text in v-menu -->
                <span class="d-inline-block pointer color text-links  mr-2">
                    Query ID
                    <!-- : {{ query_result.queryId }} -->
                </span>
                <span class="d-inline-block pointer color text-links ">
                    Query text
                </span>
            </div>
            <v-skeleton-loader
                v-if="loading_query_result"
                :loading="loading_query_result"
                type="table: table-thead, table-tbody"
                :max-height="`${dynDim.height - headerHeight}px`"
            />
            <result-data-table
                v-else
                :height="dynDim.height - headerHeight"
                :width="dynDim.width"
                :headers="tableHeaders"
                :rows="tableRows"
            />
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
import { mapState } from 'vuex'
import ResultDataTable from './ResultDataTable'
export default {
    name: 'result-tab',
    components: {
        ResultDataTable,
    },
    props: {
        queryTxt: { type: String, require: true },
        dynDim: {
            type: Object,
            validator(obj) {
                return 'width' in obj && 'height' in obj
            },
            required: true,
        },
    },
    data() {
        return {
            headerHeight: 0,
            isMounted: true,
        }
    },
    computed: {
        ...mapState({
            query_result: state => state.query.query_result,
            loading_query_result: state => state.query.loading_query_result,
        }),
        tableHeaders() {
            if (!this.query_result.fields) return []
            return this.query_result.fields
        },
        tableRows() {
            if (!this.query_result.data) return []
            return this.query_result.data
        },
    },
    watch: {
        loading_query_result(v) {
            // After user clicks Run to send query, set isMounted to false to show skeleton-loader
            if (v && this.isMounted) this.isMounted = false
        },
    },
    mounted() {
        this.setHeaderHeight()
    },
    methods: {
        setHeaderHeight() {
            if (!this.$refs.header) return
            this.headerHeight = this.$refs.header.clientHeight
        },
    },
}
</script>
