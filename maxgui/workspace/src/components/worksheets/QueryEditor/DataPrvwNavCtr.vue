<template>
    <v-tabs v-model="activeMode" hide-slider :height="20" class="v-tabs--mxs-workspace-style">
        <v-tab
            :key="QUERY_MODES.PRVW_DATA"
            :href="`#${QUERY_MODES.PRVW_DATA}`"
            class="tab-btn px-3 text-uppercase"
            active-class="tab-btn--active font-weight-medium"
        >
            {{ $mxs_t('data') }}
        </v-tab>
        <v-tab
            :key="QUERY_MODES.PRVW_DATA_DETAILS"
            :href="`#${QUERY_MODES.PRVW_DATA_DETAILS}`"
            class="tab-btn px-3 text-uppercase"
            active-class="tab-btn--active font-weight-medium"
        >
            {{ $mxs_t('details') }}
        </v-tab>
    </v-tabs>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import QueryResult from '@wsModels/QueryResult'
import { QUERY_MODES } from '@wsSrc/constants'

export default {
    name: 'data-prvw-nav-ctr',
    props: {
        queryTabId: { type: String, required: true },
        queryMode: { type: String, required: true },
        isLoading: { type: Boolean, required: true },
        resultData: { type: Object, required: true },
        nodeQualifiedName: { type: String, required: true },
    },
    computed: {
        activeMode: {
            get() {
                return this.queryMode
            },
            set(v) {
                if (
                    this.queryMode === this.QUERY_MODES.PRVW_DATA ||
                    this.queryMode === this.QUERY_MODES.PRVW_DATA_DETAILS
                )
                    QueryResult.update({ where: this.queryTabId, data: { query_mode: v } })
            },
        },
    },
    watch: {
        activeMode: async function(activeMode) {
            if (!this.isLoading) await this.handleFetch(activeMode)
        },
    },
    created() {
        this.QUERY_MODES = QUERY_MODES
    },
    methods: {
        /**
         * This function checks if there is no preview data or details data
         * before dispatching action to fetch either preview data or details
         * data based on query_mode value.
         * @param {String} query_mode - query mode
         */
        async handleFetch(query_mode) {
            switch (query_mode) {
                case this.QUERY_MODES.PRVW_DATA:
                case this.QUERY_MODES.PRVW_DATA_DETAILS:
                    if (!this.resultData.fields) {
                        await QueryResult.dispatch('fetchPrvw', {
                            qualified_name: this.nodeQualifiedName,
                            query_mode,
                        })
                    }
                    break
            }
        },
    },
}
</script>
