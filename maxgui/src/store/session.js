/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-08-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    namespaced: true,
    state: {
        all_sessions: [],
        sessions_datasets: [],
        sessions_by_service: [],
    },
    mutations: {
        SET_ALL_SESSIONS(state, payload) {
            state.all_sessions = payload
        },
        SET_SESSIONS_DATASETS(state, payload) {
            state.sessions_datasets = payload
        },
        SET_SESSIONS_BY_SERVICE(state, payload) {
            state.sessions_by_service = payload
        },
    },
    actions: {
        async fetchAllSessions({ commit }) {
            try {
                let res = await this.vue.$axios.get(`/sessions`)
                if (res.data.data) commit('SET_ALL_SESSIONS', res.data.data)
            } catch (e) {
                if (process.env.NODE_ENV !== 'test') {
                    const logger = this.vue.$logger('store-sessions-fetchAllSessions')
                    logger.error(e)
                }
            }
        },

        genDataSets({ commit, state }) {
            const { all_sessions } = state
            const { genLineDataSet } = this.vue.$help
            const dataset = genLineDataSet('Total sessions', all_sessions.length, 0)
            commit('SET_SESSIONS_DATASETS', [dataset])
        },

        //-------------------- sessions filter by relationships serviceId
        async fetchSessionsFilterByService({ commit }, id) {
            let res = await this.vue.$axios.get(
                `/sessions?filter=/relationships/services/data/0/id="${id}"`
            )
            commit('SET_SESSIONS_BY_SERVICE', res.data.data)
        },
    },
}
