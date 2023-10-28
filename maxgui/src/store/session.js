/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
const getDefPaginationConfig = () => ({
    page: 0,
    itemsPerPage: 50,
})
export default {
    namespaced: true,
    state: {
        pagination_config: getDefPaginationConfig(),
        current_sessions: [], //sessions on dashboard
        total_sessions: 0,
        sessions_datasets: [],
        filtered_sessions: [],
        total_filtered_sessions: 0,
    },
    mutations: {
        SET_PAGINATION_CONFIG(state, payload) {
            state.pagination_config = payload
        },
        SET_DEF_PAGINATION_CONFIG(state) {
            state.pagination_config = getDefPaginationConfig()
        },

        SET_CURRENT_SESSIONS(state, payload) {
            state.current_sessions = payload
        },
        SET_TOTAL_SESSIONS(state, payload) {
            state.total_sessions = payload
        },

        SET_SESSIONS_DATASETS(state, payload) {
            state.sessions_datasets = payload
        },

        SET_FILTERED_SESSIONS(state, payload) {
            state.filtered_sessions = payload
        },
        SET_TOTAL_FILTERED_SESSIONS(state, payload) {
            state.total_filtered_sessions = payload
        },
    },
    actions: {
        async fetchSessions({ commit, getters }) {
            try {
                const paginateParam = getters.getPaginateParam
                let res = await this.$http.get(
                    `/sessions${paginateParam ? `?${paginateParam}` : ''}`
                )
                if (res.data.data) {
                    commit('SET_CURRENT_SESSIONS', res.data.data)
                    const total = this.vue.$typy(res, 'data.meta.total').safeNumber
                    commit('SET_TOTAL_SESSIONS', total ? total : res.data.data.length)
                }
            } catch (e) {
                const logger = this.vue.$logger('store-sessions-fetchSessions')
                logger.error(e)
            }
        },

        genDataSets({ commit, state }) {
            const { genLineStreamDataset } = this.vue.$help
            const dataset = genLineStreamDataset({
                label: 'Total sessions',
                value: state.total_sessions,
                colorIndex: 0,
            })
            commit('SET_SESSIONS_DATASETS', [dataset])
        },

        async fetchSessionsWithFilter({ getters, commit }, filterParam) {
            try {
                const paginateParam = getters.getPaginateParam
                let res = await this.$http.get(
                    `/sessions?${filterParam}${paginateParam ? `&${paginateParam}` : ''}`
                )
                if (res.data.data) {
                    commit('SET_FILTERED_SESSIONS', res.data.data)
                    const total = this.vue.$typy(res, 'data.meta.total').safeNumber
                    commit('SET_TOTAL_FILTERED_SESSIONS', total ? total : res.data.data.length)
                }
            } catch (e) {
                const logger = this.vue.$logger('store-sessions-fetchSessionsWithFilter')
                logger.error(e)
            }
        },
    },
    getters: {
        getTotalSessions: state => state.total_sessions,
        getTotalFilteredSessions: state => state.total_filtered_sessions,
        getPaginateParam: ({ pagination_config: { itemsPerPage, page } }) =>
            itemsPerPage === -1 ? '' : `page[size]=${itemsPerPage}&page[number]=${page}`,
        getFilterParamByServiceId: () => serviceId =>
            `filter=/relationships/services/data/0/id="${serviceId}"`,
        getFilterParamByServerId: () => serverId =>
            `filter=/attributes/connections/0/server="${serverId}"`,
    },
}