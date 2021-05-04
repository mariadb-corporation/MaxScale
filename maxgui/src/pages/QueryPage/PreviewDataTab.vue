<template>
    <div class="fill-height">
        <div ref="header" class="pb-2 result-header">
            <div v-if="previewDataSchemaId" class="schema-view-title">
                <span><b>Table:</b> {{ previewDataSchemaId }}</span>
                <v-btn-toggle v-model="activeView" class="ml-4" mandatory>
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
                :max-height="`${dynDim.height - headerHeight}px`"
            />
            <template v-else>
                <result-data-table
                    v-if="activeView === SQL_QUERY_MODES.PREVIEW_DATA"
                    :key="SQL_QUERY_MODES.PREVIEW_DATA"
                    :height="dynDim.height - headerHeight"
                    :width="dynDim.width"
                    :headers="previewDataHeaders"
                    :rows="previewDataRows"
                />
                <result-data-table
                    v-else
                    :key="SQL_QUERY_MODES.VIEW_DETAILS"
                    :height="dynDim.height - headerHeight"
                    :width="dynDim.width"
                    :headers="detailsDataHeaders"
                    :rows="detailsDataRows"
                />
            </template>
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
import { mapState, mapActions } from 'vuex'
import ResultDataTable from './ResultDataTable'
export default {
    name: 'preview-data-tab',
    components: {
        ResultDataTable,
    },
    props: {
        previewDataSchemaId: { type: String, require: true },
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
        previewDataHeaders() {
            if (!this.preview_data.fields) return []
            return this.preview_data.fields
        },
        previewDataRows() {
            if (!this.preview_data.data) return []
            return this.preview_data.data
        },
        detailsDataHeaders() {
            if (!this.data_details.fields) return []
            return this.data_details.fields
        },
        detailsDataRows() {
            if (!this.data_details.data) return []
            return this.data_details.data
        },
        isPrwDataLoading() {
            return this.loading_preview_data || this.loading_data_details
        },
        activeView: {
            get() {
                return this.curr_query_mode
            },
            set(value) {
                if (this.curr_query_mode !== this.SQL_QUERY_MODES.QUERY_VIEW)
                    this.setCurrQueryMode(value)
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
                    if (!this.preview_data.fields)
                        await this.fetchPreviewData(this.previewDataSchemaId)
                    break
                case this.SQL_QUERY_MODES.VIEW_DETAILS:
                    if (!this.data_details.fields)
                        await this.fetchDataDetails(this.previewDataSchemaId)
                    break
            }
        },
    },
}
</script>
