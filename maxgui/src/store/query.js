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
        loading_preview_data: false,
        preview_data: {},
        loading_data_details: false,
        data_details: {},
        loading_query_result: false,
        query_result: {},
        curr_query_mode: '',
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
        SET_CURR_QUERY_MODE(state, payload) {
            state.curr_query_mode = payload
        },
    },
    actions: {
        async fetchConnectionSchema({ commit }) {
            try {
                commit('SET_LOADING_SCHEMA', true)
                // TODO: Replace with actual data
                /* let res = await this.vue.$axios.get(`/query/schema`)
                if (res.data.data) commit('SET_CONN_SCHEMA', Object.freeze(res.data.data)) */
                await this.vue.$help.delay(400)
                commit('SET_CONN_SCHEMA', Object.freeze(dummy_schema_test))
                commit('SET_LOADING_SCHEMA', false)
            } catch (e) {
                const logger = this.vue.$logger('store-query-fetchConnectionSchema')
                logger.error(e)
            }
        },

        //TODO: DRY fetchPreviewData and fetchDataDetails actions
        async fetchPreviewData({ commit }, schemaId) {
            try {
                commit('SET_LOADING_PREVIEW_DATA', true)
                const query = `SELECT * FROM ${schemaId};`
                //TODO: for testing purpose, replace it with config obj
                const body = {
                    user: 'maxskysql',
                    password: 'skysql',
                    sql: query,
                    timeout: 5,
                    db: 'mysql',
                }
                let res = await this.vue.$axios.post(`/servers/server_0/query`, body)
                if (res.data) {
                    await this.vue.$help.delay(400)
                    commit('SET_PREVIEW_DATA', Object.freeze(res.data))
                    commit('SET_LOADING_PREVIEW_DATA', false)
                }
            } catch (e) {
                const logger = this.vue.$logger('store-query-fetchPreviewData')
                logger.error(e)
            }
        },
        async fetchDataDetails({ commit }, schemaId) {
            try {
                commit('SET_LOADING_DATA_DETAILS', true)
                const query = `DESCRIBE ${schemaId};`
                //TODO: for testing purpose, replace it with config obj
                const body = {
                    user: 'maxskysql',
                    password: 'skysql',
                    sql: query,
                    timeout: 5,
                    db: 'mysql',
                }
                let res = await this.vue.$axios.post(`/servers/server_0/query`, body)
                if (res.data) {
                    await this.vue.$help.delay(400)
                    commit('SET_DATA_DETAILS', Object.freeze(res.data))
                    commit('SET_LOADING_DATA_DETAILS', false)
                }
            } catch (e) {
                const logger = this.vue.$logger('store-query-fetchDataDetails')
                logger.error(e)
            }
        },
        /**
         * This action clears preview_data and data_details to empty object.
         * Call this action when user selects option in the sidebar.
         * This ensure sub-tabs in Data Preview tab are generated with fresh data
         */
        clearDataPreview({ commit }) {
            commit('SET_PREVIEW_DATA', {})
            commit('SET_DATA_DETAILS', {})
        },
        async fetchQueryResult({ commit }, query) {
            try {
                commit('SET_LOADING_QUERY_RESULT', true)
                //TODO: for testing purpose, replace it with config obj
                const body = {
                    user: 'maxskysql',
                    password: 'skysql',
                    sql: query,
                    timeout: 5,
                    db: 'mysql',
                }
                let res = await this.vue.$axios.post(`/servers/server_0/query`, body)
                if (res.data) {
                    await this.vue.$help.delay(400)
                    commit('SET_QUERY_RESULT', Object.freeze(res.data))
                    commit('SET_LOADING_QUERY_RESULT', false)
                }
            } catch (e) {
                const logger = this.vue.$logger('store-query-fetchQueryResult')
                logger.error(e)
            }
        },
        setCurrQueryMode({ commit }, mode) {
            try {
                commit('SET_CURR_QUERY_MODE', mode)
            } catch (e) {
                const logger = this.vue.$logger('store-query-setCurrQueryMode')
                logger.error(e)
            }
        },
    },
}
