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
                    :dim="tabDim"
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
                    :dim="tabDim"
                    :queryMode="queryMode"
                    :queryTabId="queryTabId"
                    :queryTabTmp="queryTabTmp"
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
                    :dim="tabDim"
                    :queryMode="queryMode"
                    :queryTabId="queryTabId"
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
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import QueryResult from '@wsModels/QueryResult'
import DataPrvw from '@wkeComps/QueryEditor/DataPrvw.vue'
import ResultsTab from '@wkeComps/QueryEditor/ResultsTab.vue'
import HistoryAndSnippetsCtr from '@wkeComps/QueryEditor/HistoryAndSnippetsCtr.vue'
import { QUERY_MODES } from '@wsSrc/constants'

export default {
    name: 'query-result-ctr',
    components: {
        DataPrvw,
        ResultsTab,
        HistoryAndSnippetsCtr,
    },
    props: {
        dim: {
            type: Object,
            validator(obj) {
                return 'width' in obj && 'height' in obj
            },
            required: true,
        },
        queryTab: { type: Object, required: true },
        queryTabConn: { type: Object, required: true },
        queryTabTmp: { type: Object, required: true },
    },
    data() {
        return {
            tabItemClass: 'pt-2 px-5 mxs-field-text-size mxs-color-helper text-small-text',
        }
    },
    computed: {
        queryTabId() {
            return this.$typy(this.queryTab, 'id').safeString
        },
        isConnBusy() {
            return this.$typy(this.queryTabConn, 'is_busy').safeBoolean
        },
        tabDim() {
            /*
             * width: dim.width - px-5
             * height: dim.height - $tab-bar-height - pt-2
             */
            return { width: this.dim.width - 40, height: this.dim.height - 24 - 8 }
        },
        queryMode() {
            return this.$typy(QueryResult.find(this.queryTabId), 'query_mode').safeString
        },
        activeTab: {
            get() {
                switch (this.queryMode) {
                    case this.QUERY_MODES.PRVW_DATA_DETAILS:
                    case this.QUERY_MODES.PRVW_DATA:
                        return this.QUERY_MODES.PRVW_DATA
                    case this.QUERY_MODES.SNIPPETS:
                    case this.QUERY_MODES.HISTORY:
                        return this.QUERY_MODES.HISTORY
                    default:
                        return this.queryMode
                }
            },
            set(v) {
                QueryResult.update({ where: this.queryTabId, data: { query_mode: v } })
            },
        },
        queryData() {
            const { QUERY_VIEW, PRVW_DATA, PRVW_DATA_DETAILS } = this.QUERY_MODES
            switch (this.queryMode) {
                case QUERY_VIEW:
                    return this.$typy(this.queryTabTmp, 'query_results').safeObjectOrEmpty
                case PRVW_DATA:
                    return this.$typy(this.queryTabTmp, 'prvw_data').safeObjectOrEmpty
                case PRVW_DATA_DETAILS:
                    return this.$typy(this.queryTabTmp, 'prvw_data_details').safeObjectOrEmpty
                default:
                    return {}
            }
        },
        isLoading() {
            return this.$typy(this.queryData, 'is_loading').safeBoolean
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
    },
    created() {
        this.QUERY_MODES = QUERY_MODES
    },
}
</script>
