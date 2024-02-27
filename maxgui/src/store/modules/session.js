/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { genSetMutations } from '@share/utils/helpers'

const getDefPaginationConfig = () => ({
    page: 0,
    itemsPerPage: 50,
})

const states = () => ({
    pagination_config: getDefPaginationConfig(),
    current_sessions: [], //sessions on dashboard
    total_sessions: 0,
    sessions_datasets: [],
    filtered_sessions: [],
    total_filtered_sessions: 0,
})

export default {
    namespaced: true,
    state: states(),
    mutations: {
        SET_DEF_PAGINATION_CONFIG(state) {
            state.pagination_config = getDefPaginationConfig()
        },
        ...genSetMutations(states()),
    },
    actions: {
        async fetchSessions({ commit, getters }) {
            try {
                const paginateParam = getters.getPaginateParam
                let res = await this.vue.$http.get(
                    `/sessions${paginateParam ? `?${paginateParam}` : ''}`
                )
                if (res.data.data) {
                    commit('SET_CURRENT_SESSIONS', res.data.data)
                    const total = this.vue.$typy(res, 'data.meta.total').safeNumber
                    commit('SET_TOTAL_SESSIONS', total ? total : res.data.data.length)
                }
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },

        genDataSets({ commit, state }) {
            const { genLineStreamDataset } = this.vue.$helpers
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
                let res = await this.vue.$http.get(
                    `/sessions?${filterParam}${paginateParam ? `&${paginateParam}` : ''}`
                )
                if (res.data.data) {
                    commit('SET_FILTERED_SESSIONS', res.data.data)
                    const total = this.vue.$typy(res, 'data.meta.total').safeNumber
                    commit('SET_TOTAL_FILTERED_SESSIONS', total ? total : res.data.data.length)
                }
            } catch (e) {
                this.vue.$logger.error(e)
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
                        'mxsApp/SET_SNACK_BAR_MESSAGE',
                        {
                            text: [this.vue.$mxs_t('success.killedSession')],
                            type: 'success',
                        },
                        { root: true }
                    )
                    await this.vue.$typy(callback).safeFunction()
                }
            } catch (e) {
                this.vue.$logger.error(e)
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
