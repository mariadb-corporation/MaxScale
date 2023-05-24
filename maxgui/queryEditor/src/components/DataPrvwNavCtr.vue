<template>
    <v-tabs v-model="activeView" hide-slider :height="20" class="v-tabs--query-editor-style">
        <v-tab
            :key="SQL_QUERY_MODES.PRVW_DATA"
            :href="`#${SQL_QUERY_MODES.PRVW_DATA}`"
            class="tab-btn px-3 text-uppercase"
            active-class="tab-btn--active font-weight-medium"
        >
            {{ $mxs_t('data') }}
        </v-tab>
        <v-tab
            :key="SQL_QUERY_MODES.PRVW_DATA_DETAILS"
            :href="`#${SQL_QUERY_MODES.PRVW_DATA_DETAILS}`"
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
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapActions, mapMutations, mapGetters } from 'vuex'
export default {
    name: 'data-prvw-nav-ctr',
    props: {
        isLoading: { type: Boolean, required: true },
        resultData: { type: Object, required: true },
    },
    computed: {
        ...mapState({
            SQL_QUERY_MODES: state => state.queryEditorConfig.config.SQL_QUERY_MODES,
            curr_query_mode: state => state.queryResult.curr_query_mode,
        }),
        ...mapGetters({
            getActivePrvwTblNode: 'schemaSidebar/getActivePrvwTblNode',
            getActiveSessionId: 'querySession/getActiveSessionId',
        }),
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
            if (!this.isLoading) await this.handleFetch(activeView)
        },
    },
    methods: {
        ...mapMutations({ SET_CURR_QUERY_MODE: 'queryResult/SET_CURR_QUERY_MODE' }),
        ...mapActions({ fetchPrvw: 'queryResult/fetchPrvw' }),
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
                        await this.fetchPrvw({
                            tblId: this.$typy(this.getActivePrvwTblNode, 'id').safeObject,
                            prvwMode: SQL_QUERY_MODE,
                        })
                    }
                    break
            }
        },
    },
}
</script>
