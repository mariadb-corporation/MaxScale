<template>
    <v-tabs v-model="activeView" hide-slider :height="20" class="v-tabs--mxs-workspace-style">
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
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'
import QueryEditor from '@wsModels/QueryEditor'
import SchemaSidebar from '@wsModels/SchemaSidebar'
import QueryResult from '@wsModels/QueryResult'

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
        activeQueryMode() {
            return QueryResult.getters('getActiveQueryMode')
        },
        activeView: {
            get() {
                return this.activeQueryMode
            },
            set(v) {
                if (
                    this.activeQueryMode === this.QUERY_MODES.PRVW_DATA ||
                    this.activeQueryMode === this.QUERY_MODES.PRVW_DATA_DETAILS
                )
                    QueryResult.update({
                        where: QueryEditor.getters('getActiveQueryTabId'),
                        data: { query_mode: v },
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
                            qualified_name: SchemaSidebar.getters('getPreviewingNodeQualifiedName'),
                            query_mode,
                        })
                    }
                    break
            }
        },
    },
}
</script>
