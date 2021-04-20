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
import { preview_data, data_details } from 'utils/dummy_result_test'

export default {
    namespaced: true,
    state: {
        loading_schema: true,
        conn_schema: {},
        loading_preview_data: false,
        preview_data: {},
        loading_data_details: false,
        data_details: {},
        loading_query_result: false,
        query_result: {},
        curr_sql_query_mode: '',
    },
    mutations: {
        SET_LOADING_SCHEMA(state, payload) {
            state.loading_schema = payload
        },
        SET_CONN_SCHEMA(state, payload) {
            state.conn_schema = payload
        },
        SET_LOADING_PREVIEW_DATA(state, payload) {
            state.loading_preview_data = payload
        },
        SET_PREVIEW_DATA(state, payload) {
            state.preview_data = payload
        },
        SET_LOADING_DATA_DETAILS(state, payload) {
            state.loading_data_details = payload
        },
        SET_DATA_DETAILS(state, payload) {
            state.data_details = payload
        },
        SET_LOADING_QUERY_RESULT(state, payload) {
            state.loading_query_result = payload
        },
        SET_QUERY_RESULT(state, payload) {
            state.query_result = payload
        },
        SET_CURR_SQL_QUERY_MODE(state, payload) {
            state.curr_sql_query_mode = payload
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
        async fetchPreviewData({ commit /* , dispatch, state */ }, query) {
            try {
                commit('SET_LOADING_PREVIEW_DATA', true)
                /* eslint-disable no-console */
                console.log('sending query', query)
                // TODO: Replace with actual data
                /*      dispatch('query/fetchQueryResult', query)
                if (state.query_result) {
                    commit('SET_PREVIEW_DATA', state.query_result)
                    commit('SET_LOADING_PREVIEW_DATA', false)
                }
                */
                await this.vue.$help.delay(400)
                commit('SET_PREVIEW_DATA', preview_data.data)
                commit('SET_LOADING_PREVIEW_DATA', false)
            } catch (e) {
                const logger = this.vue.$logger('store-query-fetchPreviewData')
                logger.error(e)
            }
        },
        async fetchDataDetails({ commit /* , dispatch, state*/ }, query) {
            try {
                commit('SET_LOADING_DATA_DETAILS', true)
                /* eslint-disable no-console */
                console.log('sending query', query)
                // TODO: Replace with actual data
                /*
                    dispatch("query/fetchQueryResult",query)
                if (state.query_result) {
                    commit('SET_DATA_DETAILS', state.query_result)
                    commit('SET_LOADING_DATA_DETAILS', false)
                } */
                await this.vue.$help.delay(400)
                commit('SET_DATA_DETAILS', data_details.data)
                commit('SET_LOADING_DATA_DETAILS', false)
            } catch (e) {
                const logger = this.vue.$logger('store-query-fetchDataDetails')
                logger.error(e)
            }
        },
        async fetchQueryResult({ commit }, query) {
            try {
                commit('SET_LOADING_QUERY_RESULT', true)
                /* eslint-disable no-console */
                console.log('sending query', query)
                // TODO: Replace with actual data
                /*    const body = {
                    data: {
                        sqlText: query,
                    },
                }
                let res = await this.vue.$axios.post(`/query/`, body)
                if (res.data.data) {
                    commit('SET_QUERY_RESULT', res.data.data)
                    commit('SET_LOADING_QUERY_RESULT', false)
                } */
                await this.vue.$help.delay(400)
                commit('SET_QUERY_RESULT', preview_data.data)
                commit('SET_LOADING_QUERY_RESULT', false)
            } catch (e) {
                const logger = this.vue.$logger('store-query-fetchQueryResult')
                logger.error(e)
            }
        },
        async switchSQLMode({ commit }, mode) {
            try {
                commit('SET_CURR_SQL_QUERY_MODE', mode)
            } catch (e) {
                const logger = this.vue.$logger('store-query-switchSQLMode')
                logger.error(e)
            }
        },
    },
}
