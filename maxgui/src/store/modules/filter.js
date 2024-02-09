/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { genSetMutations } from '@share/utils/helpers'

const states = () => ({
    all_filters: [],
    current_filter: {},
})

export default {
    namespaced: true,
    state: states(),
    mutations: genSetMutations(states()),
    actions: {
        async fetchAllFilters({ commit }) {
            try {
                let res = await this.vue.$http.get(`/filters`)
                if (res.data.data) commit('SET_ALL_FILTERS', res.data.data)
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },

        async fetchFilterById({ commit }, id) {
            try {
                let res = await this.vue.$http.get(`/filters/${id}`)
                if (res.data.data) commit('SET_CURRENT_FILTER', res.data.data)
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },

        /**
         * @param {Object} payload payload object for creating filter
         * @param {String} payload.id Name of the filter
         * @param {String} payload.module The filter module to use
         * @param {Object} payload.parameters Parameters for the filter
         * @param {Function} payload.callback callback function after successfully updated
         */
        async createFilter({ commit }, payload) {
            try {
                const body = {
                    data: {
                        id: payload.id,
                        type: 'filters',
                        attributes: {
                            module: payload.module,
                            parameters: payload.parameters,
                        },
                    },
                }
                let res = await this.vue.$http.post(`/filters`, body)
                let message = [`Filter ${payload.id} is created`]
                // response ok
                if (res.status === 204) {
                    commit(
                        'mxsApp/SET_SNACK_BAR_MESSAGE',
                        {
                            text: message,
                            type: 'success',
                        },
                        { root: true }
                    )
                    await this.vue.$typy(payload.callback).safeFunction()
                }
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },

        /**
         * @param {Object} payload payload object
         * @param {String} payload.id Name of the filter
         * @param {Object} payload.parameters Parameters for the filter
         * @param {Function} payload.callback callback function after successfully updated
         */
        async updateFilterParameters({ commit }, payload) {
            try {
                const body = {
                    data: {
                        id: payload.id,
                        type: 'filters',
                        attributes: { parameters: payload.parameters },
                    },
                }
                let res = await this.vue.$http.patch(`/filters/${payload.id}`, body)
                // response ok
                if (res.status === 204) {
                    commit(
                        'mxsApp/SET_SNACK_BAR_MESSAGE',
                        {
                            text: [`Parameters of ${payload.id} is updated`],
                            type: 'success',
                        },
                        { root: true }
                    )
                    await this.vue.$typy(payload.callback).safeFunction()
                }
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },

        async destroyFilter({ dispatch, commit }, id) {
            try {
                let res = await this.vue.$http.delete(`/filters/${id}?force=yes`)
                if (res.status === 204) {
                    await dispatch('fetchAllFilters')
                    commit(
                        'mxsApp/SET_SNACK_BAR_MESSAGE',
                        {
                            text: [`Filter ${id} is destroyed`],
                            type: 'success',
                        },
                        { root: true }
                    )
                }
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },
    },
    getters: {
        // -------------- below getters are available only when fetchAllFilters has been dispatched
        getTotalFilters: state => state.all_filters.length,
        getAllFiltersMap: state => {
            let map = new Map()
            state.all_filters.forEach(ele => {
                map.set(ele.id, ele)
            })
            return map
        },
    },
}
