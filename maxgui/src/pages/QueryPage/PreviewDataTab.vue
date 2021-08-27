<template>
    <div class="fill-height">
        <div ref="header" class="pb-2 result-header d-flex align-center">
            <template v-if="validConn">
                <div class="d-flex align-center mr-4">
                    <b class="mr-1">Table:</b>
                    <truncate-string :maxWidth="260" :nudgeLeft="16" :text="previewDataSchemaId" />
                </div>
                <v-tabs
                    v-model="activeView"
                    hide-slider
                    :height="20"
                    class="tab-navigation--btn-style"
                >
                    <v-tab
                        :key="SQL_QUERY_MODES.PRVW_DATA"
                        :href="`#${SQL_QUERY_MODES.PRVW_DATA}`"
                        class="tab-btn px-3 text-uppercase"
                        active-class="tab-btn--active font-weight-medium"
                    >
                        {{ $t('data') }}
                    </v-tab>
                    <v-tab
                        :key="SQL_QUERY_MODES.PRVW_DATA_DETAILS"
                        :href="`#${SQL_QUERY_MODES.PRVW_DATA_DETAILS}`"
                        class="tab-btn px-3 text-uppercase"
                        active-class="tab-btn--active font-weight-medium"
                    >
                        {{ $t('details') }}
                    </v-tab>
                </v-tabs>
                <v-spacer />
                <keep-alive>
                    <duration-timer
                        v-if="activeView === SQL_QUERY_MODES.PRVW_DATA"
                        :key="SQL_QUERY_MODES.PRVW_DATA"
                        :startTime="prvw_data_request_sent_time"
                        :executionTime="getPrvwExeTime(activeView)"
                    />
                    <duration-timer
                        v-else-if="activeView === SQL_QUERY_MODES.PRVW_DATA_DETAILS"
                        :startTime="prvw_data_details_request_sent_time"
                        :executionTime="getPrvwExeTime(activeView)"
                    />
                </keep-alive>

                <v-tooltip
                    v-if="activeView === SQL_QUERY_MODES.PRVW_DATA"
                    top
                    transition="slide-y-transition"
                    content-class="shadow-drop color text-navigation py-1 px-4"
                >
                    <template v-slot:activator="{ on }">
                        <div
                            v-if="!getPrvwDataRes(SQL_QUERY_MODES.PRVW_DATA).complete"
                            class="ml-4 d-flex align-center"
                            v-on="on"
                        >
                            <v-icon size="16" color="error" class="mr-2">
                                $vuetify.icons.alertWarning
                            </v-icon>
                            {{ $t('incomplete') }}
                        </div>
                    </template>
                    <span> {{ $t('info.queryIncomplete') }}</span>
                </v-tooltip>
            </template>
            <span v-else v-html="$t('prvwTabGuide')" />
        </div>
        <template v-if="validConn">
            <v-skeleton-loader
                v-if="isPrwDataLoading"
                :loading="isPrwDataLoading"
                type="table: table-thead, table-tbody"
                :height="dynDim.height - headerHeight"
            />
            <template v-else>
                <v-slide-x-transition>
                    <keep-alive>
                        <result-data-table
                            v-if="activeView === SQL_QUERY_MODES.PRVW_DATA"
                            :key="SQL_QUERY_MODES.PRVW_DATA"
                            :height="dynDim.height - headerHeight"
                            :width="dynDim.width"
                            :headers="getPrvwDataRes(activeView).fields"
                            :rows="getPrvwDataRes(activeView).data"
                        />

                        <result-data-table
                            v-else-if="activeView === SQL_QUERY_MODES.PRVW_DATA_DETAILS"
                            :key="SQL_QUERY_MODES.PRVW_DATA_DETAILS"
                            :height="dynDim.height - headerHeight"
                            :width="dynDim.width"
                            :headers="getPrvwDataRes(activeView).fields"
                            :rows="getPrvwDataRes(activeView).data"
                        />
                    </keep-alive>
                </v-slide-x-transition>
            </template>
        </template>
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
import { mapState, mapActions, mapMutations, mapGetters } from 'vuex'
import ResultDataTable from './ResultDataTable'
import DurationTimer from './DurationTimer'
export default {
    name: 'preview-data-tab',
    components: {
        ResultDataTable,
        DurationTimer,
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
            loading_prvw_data: state => state.query.loading_prvw_data,
            loading_prvw_data_details: state => state.query.loading_prvw_data_details,
            prvw_data_request_sent_time: state => state.query.prvw_data_request_sent_time,
            prvw_data_details_request_sent_time: state =>
                state.query.prvw_data_details_request_sent_time,
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
            curr_query_mode: state => state.query.curr_query_mode,
            active_conn_state: state => state.query.active_conn_state,
        }),
        ...mapGetters({
            getPrvwDataRes: 'query/getPrvwDataRes',
            getPrvwExeTime: 'query/getPrvwExeTime',
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
        activeView: async function(activeView) {
            // Wait until data is fetched
            if (!this.isPrwDataLoading && this.validConn) await this.handleFetch(activeView)
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
                case this.SQL_QUERY_MODES.PRVW_DATA_DETAILS:
                    if (!this.getPrvwDataRes(SQL_QUERY_MODE).fields)
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
