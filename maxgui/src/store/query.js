/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import dummy_schema_test from 'utils/dummy_schema_test'

export default {
    namespaced: true,
    state: {
        loading_schema: true,
        conn_schema: {},
    },
    mutations: {
        SET_LOADING_SCHEMA(state, payload) {
            state.loading_schema = payload
        },
        SET_CONN_SCHEMA(state, payload) {
            state.conn_schema = payload
        },
    },
    actions: {
        async fetchConnectionSchema({ commit }) {
            try {
                commit('SET_LOADING_SCHEMA', true)
                // TODO: Replace with actual data
                /* let res = await this.vue.$axios.get(`/query/schema`)
                if (res.data.data) commit('SET_CONN_SCHEMA', res.data.data) */
                await this.vue.$help.delay(400)
                commit('SET_CONN_SCHEMA', dummy_schema_test)
                commit('SET_LOADING_SCHEMA', false)
            } catch (e) {
                const logger = this.vue.$logger('store-query-fetchConnectionSchema')
                logger.error(e)
            }
        },
    },
}
