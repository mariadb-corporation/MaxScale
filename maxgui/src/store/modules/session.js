/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-08-25
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
                let res = await this.vue.$http.get(`/sessions`)
                if (res.data.data) commit('SET_ALL_SESSIONS', res.data.data)
            } catch (e) {
                const logger = this.vue.$logger('store-sessions-fetchAllSessions')
                logger.error(e)
            }
        },

        genDataSets({ commit, state }) {
            const { all_sessions } = state
            const { genLineStreamDataset } = this.vue.$helpers
            const dataset = genLineStreamDataset({
                label: 'Total sessions',
                value: all_sessions.length,
                colorIndex: 0,
            })
            commit('SET_SESSIONS_DATASETS', [dataset])
        },

        async fetchSessionsFilterByService({ commit }, id) {
            try {
                let res = await this.vue.$http.get(
                    `/sessions?filter=/relationships/services/data/0/id="${id}"`
                )
                if (res.data.data) commit('SET_SESSIONS_BY_SERVICE', res.data.data)
            } catch (e) {
                const logger = this.vue.$logger('store-sessions-fetchSessionsFilterByService')
                logger.error(e)
            }
        },

        /**
         * @param {String} param.id - id of the session
         * @param {Function} param.callback callback function after successfully delete
         */
        async killSession({ commit }, { id, callback }) {
            try {
                const res = await this.vue.$http.delete(`/sessions/${id}`)
                if (res.status === 200) {
                    commit(
                        'appNotifier/SET_SNACK_BAR_MESSAGE',
                        {
                            text: [this.vue.$mxs_t('info.killSessionSuccessfully')],
                            type: 'success',
                        },
                        { root: true }
                    )
                    await this.vue.$typy(callback).safeFunction()
                }
            } catch (e) {
                const logger = this.vue.$logger('store-sessions-killSession')
                logger.error(e)
            }
        },
    },
}
