<template>
    <div class="fill-height">
        <div ref="header" class="pb-2 result-header">
            <div v-if="validConn" class="schema-view-title">
                <span><b>Table:</b> {{ previewDataSchemaId }}</span>
                <v-btn-toggle v-model="activeView" class="ml-4" mandatory>
                    <v-btn :value="SQL_QUERY_MODES.PRVW_DATA" x-small text color="primary">
                        {{ $t('data') }}
                    </v-btn>
                    <v-btn :value="SQL_QUERY_MODES.PRVW_DATA_DETAILS" x-small text color="primary">
                        {{ $t('details') }}
                    </v-btn>
                </v-btn-toggle>
            </div>
            <span v-else v-html="$t('prvwTabGuide')" />
        </div>

        <div v-if="validConn" class="result-table-wrapper">
            <v-skeleton-loader
                v-if="isPrwDataLoading"
                :loading="isPrwDataLoading"
                type="table: table-thead, table-tbody"
                :max-height="`${dynDim.height - headerHeight}px`"
            />
            <template v-else>
                <v-slide-x-transition>
                    <div
                        v-if="activeView === SQL_QUERY_MODES.PRVW_DATA"
                        :key="SQL_QUERY_MODES.PRVW_DATA"
                    >
                        <result-data-table
                            :height="dynDim.height - headerHeight"
                            :width="dynDim.width"
                            :headers="prvw_data.fields"
                            :rows="prvw_data.data"
                        />
                    </div>
                    <div
                        v-else-if="activeView === SQL_QUERY_MODES.PRVW_DATA_DETAILS"
                        :key="SQL_QUERY_MODES.PRVW_DATA_DETAILS"
                    >
                        <result-data-table
                            :height="dynDim.height - headerHeight"
                            :width="dynDim.width"
                            :headers="prvw_data_details.fields"
                            :rows="prvw_data_details.data"
                        />
                    </div>
                </v-slide-x-transition>
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
import { mapState, mapActions, mapMutations } from 'vuex'
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
            prvw_data: state => state.query.prvw_data,
            loading_prvw_data: state => state.query.loading_prvw_data,
            prvw_data_details: state => state.query.prvw_data_details,
            loading_prvw_data_details: state => state.query.loading_prvw_data_details,
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
            curr_query_mode: state => state.query.curr_query_mode,
            active_conn_state: state => state.query.active_conn_state,
        }),
        validConn() {
            return this.previewDataSchemaId && this.active_conn_state
        },
        isPrwDataLoading() {
            return this.loading_prvw_data || this.loading_prvw_data_details
        },
        activeView: {
            get() {
                return this.curr_query_mode
            },
            set(value) {
                if (this.curr_query_mode !== this.SQL_QUERY_MODES.QUERY_VIEW)
                    this.SET_CURR_QUERY_MODE(value)
            },
        },
    },
    watch: {
        activeView: async function(SQL_QUERY_MODE) {
            // Wait until data is fetched
            if (!this.isPrwDataLoading && this.validConn) await this.handleFetch(SQL_QUERY_MODE)
        },
    },
    mounted() {
        this.setHeaderHeight()
    },
    methods: {
        ...mapMutations({ SET_CURR_QUERY_MODE: 'query/SET_CURR_QUERY_MODE' }),
        ...mapActions({
            fetchPrvw: 'query/fetchPrvw',
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
                case this.SQL_QUERY_MODES.PRVW_DATA:
                    if (!this.prvw_data.fields)
                        await this.fetchPrvw({
                            tblId: this.previewDataSchemaId,
                            prvwMode: SQL_QUERY_MODE,
                        })
                    break
                case this.SQL_QUERY_MODES.PRVW_DATA_DETAILS:
                    if (!this.prvw_data_details.fields)
                        await this.fetchPrvw({
                            tblId: this.previewDataSchemaId,
                            prvwMode: SQL_QUERY_MODE,
                        })
                    break
            }
        },
    },
}
</script>
