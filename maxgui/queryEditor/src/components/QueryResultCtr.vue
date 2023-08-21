<template>
    <div class="fill-height mxs-color-helper border-top-table-border">
        <v-tabs v-model="activeTab" :height="24" class="v-tabs--mariadb-style">
            <v-tab
                :disabled="getIsConnBusy && isLoading"
                color="primary"
                :href="`#${SQL_QUERY_MODES.QUERY_VIEW}`"
            >
                <span> {{ $mxs_t('results') }} </span>
            </v-tab>
            <v-tab
                :disabled="getIsConnBusy && isLoading"
                color="primary"
                :href="`#${SQL_QUERY_MODES.PRVW_DATA}`"
            >
                <span>{{ $mxs_t('dataPrvw') }} </span>
            </v-tab>
            <v-tab color="primary" :href="`#${SQL_QUERY_MODES.HISTORY}`">
                <span>{{ $mxs_t('historyAndSnippets') }} </span>
            </v-tab>
        </v-tabs>
        <v-slide-x-transition>
            <keep-alive>
                <results-tab
                    v-if="activeTab === SQL_QUERY_MODES.QUERY_VIEW"
                    :style="{
                        height: `calc(100% - 24px)`,
                    }"
                    :class="tabItemClass"
                    :dynDim="componentDynDim"
                    :isLoading="isLoading"
                    :data="queryData"
                    :requestSentTime="requestSentTime"
                    :execTime="execTime"
                    :totalDuration="totalDuration"
                    v-on="$listeners"
                />
                <data-prvw
                    v-else-if="
                        activeTab === SQL_QUERY_MODES.PRVW_DATA ||
                            activeTab === SQL_QUERY_MODES.PRVW_DATA_DETAILS
                    "
                    :style="{
                        height: `calc(100% - 24px)`,
                    }"
                    :class="tabItemClass"
                    :dynDim="componentDynDim"
                    :currQueryMode="curr_query_mode"
                    :activePrvwTblNodeId="activePrvwTblNodeId"
                    :isLoading="isLoading"
                    :data="queryData"
                    :requestSentTime="requestSentTime"
                    :execTime="execTime"
                    :totalDuration="totalDuration"
                    v-on="$listeners"
                />
                <history-and-snippets-ctr
                    v-else
                    :style="{
                        height: `calc(100% - 24px)`,
                    }"
                    :class="tabItemClass"
                    :dynDim="componentDynDim"
                    v-on="$listeners"
                />
            </keep-alive>
        </v-slide-x-transition>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-08-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapMutations, mapGetters } from 'vuex'
import DataPrvw from './DataPrvw.vue'
import ResultsTab from './ResultsTab.vue'
import HistoryAndSnippetsCtr from './HistoryAndSnippetsCtr.vue'
export default {
    name: 'query-result-ctr',
    components: {
        DataPrvw,
        ResultsTab,
        HistoryAndSnippetsCtr,
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
            tabItemClass: 'pt-2 px-5 query-result-ctr-fontStyle mxs-color-helper text-small-text',
        }
    },
    computed: {
        ...mapState({
            SQL_QUERY_MODES: state => state.queryEditorConfig.config.SQL_QUERY_MODES,
            curr_query_mode: state => state.queryResult.curr_query_mode,
        }),
        ...mapGetters({
            getIsConnBusy: 'queryConn/getIsConnBusy',
            getUserQueryRes: 'queryResult/getUserQueryRes',
            getPrvwData: 'queryResult/getPrvwData',
            getActiveSessionId: 'querySession/getActiveSessionId',
            getActivePrvwTblNode: 'schemaSidebar/getActivePrvwTblNode',
        }),
        componentDynDim() {
            /*
             * width: dynDim.width - px-5
             * height: dynDim.height - $tab-bar-height - pt-2
             */
            return { width: this.dynDim.width - 40, height: this.dynDim.height - 24 - 8 }
        },
        activeTab: {
            get() {
                switch (this.curr_query_mode) {
                    case this.SQL_QUERY_MODES.PRVW_DATA_DETAILS:
                    case this.SQL_QUERY_MODES.PRVW_DATA:
                        return this.SQL_QUERY_MODES.PRVW_DATA
                    case this.SQL_QUERY_MODES.SNIPPETS:
                    case this.SQL_QUERY_MODES.HISTORY:
                        return this.SQL_QUERY_MODES.HISTORY
                    default:
                        return this.curr_query_mode
                }
            },
            set(value) {
                this.SET_CURR_QUERY_MODE({ payload: value, id: this.getActiveSessionId })
            },
        },
        isLoading() {
            return this.$typy(this.queryData, 'is_loading').safeBoolean
        },
        queryData() {
            const { QUERY_VIEW, PRVW_DATA, PRVW_DATA_DETAILS } = this.SQL_QUERY_MODES
            switch (this.curr_query_mode) {
                case QUERY_VIEW:
                    return this.getUserQueryRes
                case PRVW_DATA:
                case PRVW_DATA_DETAILS:
                    return this.getPrvwData(this.curr_query_mode)
                default:
                    return {}
            }
        },
        requestSentTime() {
            return this.$typy(this.queryData, 'request_sent_time').safeNumber
        },
        execTime() {
            if (this.isLoading) return -1
            const execution_time = this.$typy(this.queryData, 'data.attributes.execution_time')
                .safeNumber
            if (execution_time) return parseFloat(execution_time.toFixed(4))
            return 0
        },
        totalDuration() {
            return this.$typy(this.queryData, 'total_duration').safeNumber
        },
        activePrvwTblNodeId() {
            return this.$typy(this.getActivePrvwTblNode, 'id').safeString
        },
    },

    methods: {
        ...mapMutations({ SET_CURR_QUERY_MODE: 'queryResult/SET_CURR_QUERY_MODE' }),
    },
}
</script>

<style lang="scss" scoped>
.query-result-ctr-fontStyle {
    font-size: 14px;
}
</style>
