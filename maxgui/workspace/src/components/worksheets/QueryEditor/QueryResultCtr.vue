<template>
    <div class="fill-height mxs-color-helper border-top-table-border">
        <v-tabs v-model="activeTab" :height="24" class="v-tabs--mariadb">
            <v-tab
                :disabled="isConnBusy && isLoading"
                color="primary"
                :href="`#${QUERY_MODES.QUERY_VIEW}`"
            >
                <span> {{ $mxs_t('results') }} </span>
            </v-tab>
            <v-tab
                :disabled="isConnBusy && isLoading"
                color="primary"
                :href="`#${QUERY_MODES.PRVW_DATA}`"
            >
                <span>{{ $mxs_t('dataPrvw') }} </span>
            </v-tab>
            <v-tab color="primary" :href="`#${QUERY_MODES.HISTORY}`">
                <span>{{ $mxs_t('historyAndSnippets') }} </span>
            </v-tab>
        </v-tabs>
        <v-slide-x-transition>
            <keep-alive>
                <results-tab
                    v-if="activeTab === QUERY_MODES.QUERY_VIEW"
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
                        activeTab === QUERY_MODES.PRVW_DATA ||
                            activeTab === QUERY_MODES.PRVW_DATA_DETAILS
                    "
                    :style="{
                        height: `calc(100% - 24px)`,
                    }"
                    :class="tabItemClass"
                    :dynDim="componentDynDim"
                    :activeQueryMode="activeQueryMode"
                    :activePrvwNodeQualifiedName="activePrvwNodeQualifiedName"
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
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-03-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'
import QueryEditor from '@wsModels/QueryEditor'
import SchemaSidebar from '@wsModels/SchemaSidebar'
import QueryConn from '@wsModels/QueryConn'
import QueryResult from '@wsModels/QueryResult'
import DataPrvw from '@wkeComps/QueryEditor/DataPrvw.vue'
import ResultsTab from '@wkeComps/QueryEditor/ResultsTab.vue'
import HistoryAndSnippetsCtr from '@wkeComps/QueryEditor/HistoryAndSnippetsCtr.vue'

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
            QUERY_MODES: state => state.mxsWorkspace.config.QUERY_MODES,
        }),
        isConnBusy() {
            return QueryConn.getters('getIsActiveQueryTabConnBusy')
        },
        componentDynDim() {
            /*
             * width: dynDim.width - px-5
             * height: dynDim.height - $tab-bar-height - pt-2
             */
            return { width: this.dynDim.width - 40, height: this.dynDim.height - 24 - 8 }
        },
        activeQueryMode() {
            return QueryResult.getters('getActiveQueryMode')
        },
        activeTab: {
            get() {
                switch (this.activeQueryMode) {
                    case this.QUERY_MODES.PRVW_DATA_DETAILS:
                    case this.QUERY_MODES.PRVW_DATA:
                        return this.QUERY_MODES.PRVW_DATA
                    case this.QUERY_MODES.SNIPPETS:
                    case this.QUERY_MODES.HISTORY:
                        return this.QUERY_MODES.HISTORY
                    default:
                        return this.activeQueryMode
                }
            },
            set(v) {
                QueryResult.update({
                    where: QueryEditor.getters('getActiveQueryTabId'),
                    data: { query_mode: v },
                })
            },
        },
        isLoading() {
            return this.$typy(this.queryData, 'is_loading').safeBoolean
        },
        queryData() {
            const { QUERY_VIEW, PRVW_DATA, PRVW_DATA_DETAILS } = this.QUERY_MODES
            switch (this.activeQueryMode) {
                case QUERY_VIEW:
                    return QueryResult.getters('getActiveUserQueryRes')
                case PRVW_DATA:
                case PRVW_DATA_DETAILS:
                    return QueryResult.getters('getActivePrvwData')(this.activeQueryMode)
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
        activePrvwNodeQualifiedName() {
            return SchemaSidebar.getters('getActivePrvwNodeFQN')
        },
    },
}
</script>

<style lang="scss" scoped>
.query-result-ctr-fontStyle {
    font-size: 14px;
}
</style>
