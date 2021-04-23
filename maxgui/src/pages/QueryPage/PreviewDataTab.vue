<template>
    <div class="fill-height">
        <div ref="header" class="pb-2 result-header">
            <div v-if="previewDataSchemaId" class="schema-view-title">
                <span><b>Table:</b> {{ previewDataSchemaId }}</span>
                <v-btn-toggle v-model="activeView" class="ml-4">
                    <v-btn :value="SQL_QUERY_MODES.PREVIEW_DATA" x-small text color="primary">
                        Data
                    </v-btn>
                    <v-btn :value="SQL_QUERY_MODES.VIEW_DETAILS" x-small text color="primary">
                        Details
                    </v-btn>
                </v-btn-toggle>
            </div>
            <span v-else>
                On the left sidebar, hover on the table name then click option icon (
                <v-icon size="12" color="deep-ocean">more_horiz</v-icon> ) and choose either
                <b>Preview Data</b> or
                <b>View Details</b>
                to generate data
            </span>
        </div>

        <div v-if="previewDataSchemaId" class="result-table-wrapper">
            <v-skeleton-loader
                v-if="isPrwDataLoading"
                :loading="isPrwDataLoading"
                type="table: table-thead, table-tbody"
                :max-height="`${dynHeight - headerHeight}px`"
            />
            <result-data-table
                v-else
                :height="dynHeight - headerHeight"
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
import genQryResData from 'mixins/genQryResData'
import { mapState, mapActions } from 'vuex'
import ResultDataTable from './ResultDataTable'
export default {
    name: 'preview-data-tab',
    components: {
        ResultDataTable,
    },
    mixins: [genQryResData],
    props: {
        previewDataSchemaId: { type: String, require: true },
        dynHeight: { type: Number, required: true },
    },
    data() {
        return {
            headerHeight: 0,
        }
    },
    computed: {
        ...mapState({
            preview_data: state => state.query.preview_data,
            loading_preview_data: state => state.query.loading_preview_data,
            data_details: state => state.query.data_details,
            loading_data_details: state => state.query.loading_data_details,
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
            curr_query_mode: state => state.query.curr_query_mode,
        }),
        tableHeaders() {
            switch (this.activeView) {
                case this.SQL_QUERY_MODES.PREVIEW_DATA:
                    return this.genHeaders(this.preview_data)
                case this.SQL_QUERY_MODES.VIEW_DETAILS:
                    return this.genHeaders(this.data_details)
                default:
                    return []
            }
        },
        tableRows() {
            switch (this.activeView) {
                case this.SQL_QUERY_MODES.PREVIEW_DATA:
                    return this.genRows(this.preview_data)
                case this.SQL_QUERY_MODES.VIEW_DETAILS:
                    return this.genRows(this.data_details)
                default:
                    return []
            }
        },
        isPrwDataLoading() {
            return this.loading_preview_data || this.loading_data_details
        },
        activeView: {
            get() {
                return this.curr_query_mode
            },
            set(value) {
                // v-btn-toggle return undefined when a btn is click twice
                if (value) this.setCurrQueryMode(value)
            },
        },
    },
    watch: {
        activeView: async function(SQL_QUERY_MODE) {
            // Wait until data is fetched
            if (!this.isPrwDataLoading && this.previewDataSchemaId)
                await this.handleFetch(SQL_QUERY_MODE)
        },
    },
    mounted() {
        this.setHeaderHeight()
    },
    methods: {
        ...mapActions({
            setCurrQueryMode: 'query/setCurrQueryMode',
            fetchPreviewData: 'query/fetchPreviewData',
            fetchDataDetails: 'query/fetchDataDetails',
        }),
        setHeaderHeight() {
            if (!this.$refs.header) return
            this.headerHeight = this.$refs.header.clientHeight
        },
        /**
         * This function checks if there is no preview data or details data
         * before dispatching action to fetch either preview data or details
         * data based on SQL_QUERY_MODE value.
         * @param {String} SQL_QUERY_MODE - query mode
         */
        async handleFetch(SQL_QUERY_MODE) {
            switch (SQL_QUERY_MODE) {
                case this.SQL_QUERY_MODES.PREVIEW_DATA:
                    if (!this.preview_data.columns)
                        await this.fetchPreviewData(this.previewDataSchemaId)
                    break
                case this.SQL_QUERY_MODES.VIEW_DETAILS:
                    if (!this.data_details.columns)
                        await this.fetchDataDetails(this.previewDataSchemaId)
                    break
            }
        },
    },
}
</script>
