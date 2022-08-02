<template>
    <div class="fill-height">
        <div ref="header" class="pb-2 result-header d-flex align-center">
            <template v-if="validConn">
                <div class="d-flex align-center mr-4">
                    <b class="mr-1">Table:</b>
                    <truncate-string
                        :maxWidth="260"
                        :nudgeLeft="16"
                        :text="$typy(getActiveTreeNode, 'id').safeObject"
                    />
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
                        v-if="activeView"
                        :key="activeView"
                        :startTime="prvwSentTime"
                        :executionTime="prvwExeTime"
                        :totalDuration="prvwTotalDuration"
                    />
                </keep-alive>
                <v-tooltip
                    v-if="activeView === SQL_QUERY_MODES.PRVW_DATA"
                    top
                    transition="slide-y-transition"
                    content-class="shadow-drop color text-navigation py-1 px-4"
                >
                    <template v-slot:activator="{ on }">
                        <div v-if="!resultData.complete" class="ml-4 d-flex align-center" v-on="on">
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
                v-if="isLoading"
                :loading="isLoading"
                type="table: table-thead, table-tbody"
                :height="dynDim.height - headerHeight"
            />
            <template v-else>
                <keep-alive>
                    <result-data-table
                        v-if="
                            activeView === SQL_QUERY_MODES.PRVW_DATA ||
                                activeView === SQL_QUERY_MODES.PRVW_DATA_DETAILS
                        "
                        :key="activeView"
                        :height="dynDim.height - headerHeight"
                        :width="dynDim.width"
                        :headers="
                            $typy(resultData, 'fields').safeArray.map(field => ({
                                text: field,
                            }))
                        "
                        :rows="$typy(resultData, 'data').safeArray"
                        showGroupBy
                        v-on="$listeners"
                    />
                </keep-alive>
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
 * Change Date: 2026-07-11
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
        dynDim: {
            type: Object,
            validator(obj) {
                return 'width' in obj && 'height' in obj
            },
            required: true,
        },
        isLoading: { type: Boolean, required: true },
        data: { type: Object, required: true },
    },
    data() {
        return {
            headerHeight: 0,
        }
    },
    computed: {
        ...mapState({
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
            curr_query_mode: state => state.queryResult.curr_query_mode,
            active_sql_conn: state => state.queryConn.active_sql_conn,
        }),
        ...mapGetters({
            getActiveTreeNode: 'schemaSidebar/getActiveTreeNode',
            getActiveSessionId: 'querySession/getActiveSessionId',
        }),
        resultData() {
            return this.$typy(this.data, 'data.attributes.results[0]').safeObjectOrEmpty
        },
        prvwSentTime() {
            return this.$typy(this.data, 'request_sent_time').safeNumber
        },
        prvwExeTime() {
            if (this.isLoading) return -1
            const { attributes } = this.$typy(this.data, 'data').safeObject
            if (attributes) return parseFloat(attributes.execution_time.toFixed(4))
            return 0
        },
        prvwTotalDuration() {
            return this.$typy(this.data, 'total_duration').safeNumber
        },
        validConn() {
            return Boolean(this.getActiveTreeNode.id && this.active_sql_conn.id)
        },
        activeView: {
            get() {
                return this.curr_query_mode
            },
            set(value) {
                if (
                    this.curr_query_mode === this.SQL_QUERY_MODES.PRVW_DATA ||
                    this.curr_query_mode === this.SQL_QUERY_MODES.PRVW_DATA_DETAILS
                )
                    this.SET_CURR_QUERY_MODE({ payload: value, id: this.getActiveSessionId })
            },
        },
    },
    watch: {
        activeView: async function(activeView) {
            // Wait until data is fetched
            if (!this.isLoading && this.validConn) await this.handleFetch(activeView)
        },
    },
    activated() {
        this.setHeaderHeight()
    },
    methods: {
        ...mapMutations({
            SET_CURR_QUERY_MODE: 'queryResult/SET_CURR_QUERY_MODE',
        }),
        ...mapActions({
            fetchPrvw: 'queryResult/fetchPrvw',
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
                    if (!this.resultData.fields) {
                        await this.fetchActiveNodeData(SQL_QUERY_MODE)
                    }
                    break
            }
        },
        /**
         * @param {String} SQL_QUERY_MODE - query mode
         */
        async fetchActiveNodeData(SQL_QUERY_MODE) {
            await this.fetchPrvw({
                tblId: this.$typy(this.getActiveTreeNode, 'id').safeObject,
                prvwMode: SQL_QUERY_MODE,
            })
        },
    },
}
</script>
