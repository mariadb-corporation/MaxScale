<template>
    <v-tabs v-model="activeView" hide-slider :height="20" class="v-tabs--query-editor-style">
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
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'
import Worksheet from '@queryEditorSrc/store/orm/models/Worksheet'
import SchemaSidebar from '@queryEditorSrc/store/orm/models/SchemaSidebar'
import QueryResult from '@queryEditorSrc/store/orm/models/QueryResult'

export default {
    name: 'data-prvw-nav-ctr',
    props: {
        isLoading: { type: Boolean, required: true },
        resultData: { type: Object, required: true },
    },
    computed: {
        ...mapState({
            QUERY_MODES: state => state.mxsWorkspace.config.QUERY_MODES,
        }),
        currQueryMode() {
            return QueryResult.getters('getCurrQueryMode')
        },
        activeView: {
            get() {
                return this.currQueryMode
            },
            set(v) {
                if (
                    this.currQueryMode === this.QUERY_MODES.PRVW_DATA ||
                    this.currQueryMode === this.QUERY_MODES.PRVW_DATA_DETAILS
                )
                    QueryResult.update({
                        where: Worksheet.getters('getActiveQueryTabId'),
                        data: { curr_query_mode: v },
                    })
            },
        },
    },
    watch: {
        activeView: async function(activeView) {
            // Wait until data is fetched
            if (!this.isLoading) await this.handleFetch(activeView)
        },
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
                            qualified_name: SchemaSidebar.getters('getActivePrvwNodeFQN'),
                            query_mode,
                        })
                    }
                    break
            }
        },
    },
}
</script>
