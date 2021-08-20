<template>
    <div class="fill-height">
        <div ref="header" class="pb-2 result-header d-flex align-center">
            <template v-if="validConn">
                <div class="d-flex align-center mr-4">
                    <b class="mr-1">Table:</b>
                    <truncate-string
                        :maxWidth="260"
                        :nudgeLeft="16"
                        :text="$typy(active_tree_node, 'id').safeObject"
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
                        :startTime="getPrvwSentTime(activeView)"
                        :executionTime="getPrvwExeTime(activeView)"
                        :totalDuration="getPrvwTotalDuration(activeView)"
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
                <keep-alive>
                    <result-data-table
                        v-if="
                            activeView === SQL_QUERY_MODES.PRVW_DATA ||
                                activeView === SQL_QUERY_MODES.PRVW_DATA_DETAILS
                        "
                        :key="activeView"
                        :height="dynDim.height - headerHeight"
                        :width="dynDim.width"
                        :headers="$typy(getPrvwDataRes(activeView), 'fields').safeArray"
                        :rows="$typy(getPrvwDataRes(activeView), 'data').safeArray"
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
            active_tree_node: state => state.query.active_tree_node,
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
            curr_query_mode: state => state.query.curr_query_mode,
            curr_cnct_resource: state => state.query.curr_cnct_resource,
            active_wke_id: state => state.query.active_wke_id,
        }),
        ...mapGetters({
            getPrvwDataRes: 'query/getPrvwDataRes',
            getPrvwSentTime: 'query/getPrvwSentTime',
            getPrvwExeTime: 'query/getPrvwExeTime',
            getPrvwTotalDuration: 'query/getPrvwTotalDuration',
            getLoadingPrvw: 'query/getLoadingPrvw',
        }),
        validConn() {
            return Boolean(this.active_tree_node.id && this.curr_cnct_resource.id)
        },
        isPrwDataLoading() {
            return (
                this.getLoadingPrvw(this.SQL_QUERY_MODES.PRVW_DATA) ||
                this.getLoadingPrvw(this.SQL_QUERY_MODES.PRVW_DATA_DETAILS)
            )
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
    async activated() {
        this.setHeaderHeight()
        await this.autoFetchActiveNode()
    },
    methods: {
        ...mapMutations({
            SET_CURR_QUERY_MODE: 'query/SET_CURR_QUERY_MODE',
        }),
        ...mapActions({
            fetchPrvw: 'query/fetchPrvw',
        }),
        setHeaderHeight() {
            if (!this.$refs.header) return
            this.headerHeight = this.$refs.header.clientHeight
        },
        async autoFetchActiveNode() {
            if (this.curr_cnct_resource.id)
                switch (this.activeView) {
                    // Auto fetch preview data if there is active_tree_node in localStorage
                    case this.SQL_QUERY_MODES.PRVW_DATA_DETAILS:
                    case this.SQL_QUERY_MODES.PRVW_DATA: {
                        const hasActiveNode = this.$typy(this.active_tree_node, 'id').safeObject
                        const isFetchingAlready = this.getLoadingPrvw(this.activeView)
                        const isDataEmpty = this.$typy(this.getPrvwDataRes(this.activeView))
                            .isEmptyObject
                        if (hasActiveNode && !isFetchingAlready && isDataEmpty)
                            await this.fetchActiveNodeData(this.activeView)
                    }
                }
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
                    if (!this.getPrvwDataRes(SQL_QUERY_MODE).fields) {
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
                tblId: this.$typy(this.active_tree_node, 'id').safeObject,
                prvwMode: SQL_QUERY_MODE,
            })
        },
    },
}
</script>
