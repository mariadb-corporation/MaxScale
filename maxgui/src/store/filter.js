/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-11-26
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    namespaced: true,
    state: {
        all_filters: [],
        current_filter: {},
    },
    mutations: {
        SET_ALL_FILTERS(state, payload) {
            state.all_filters = payload
        },
        SET_CURRENT_FILTER(state, payload) {
            state.current_filter = payload
        },
    },
    actions: {
        async fetchAllFilters({ commit }) {
            try {
                let res = await this.vue.$axios.get(`/filters`)
                if (res.data.data) commit('SET_ALL_FILTERS', res.data.data)
            } catch (e) {
                const logger = this.vue.$logger('store-filter-fetchAllFilters')
                logger.error(e)
            }
        },

        async fetchFilterById({ commit }, id) {
            try {
                let res = await this.vue.$axios.get(`/filters/${id}`)
                if (res.data.data) commit('SET_CURRENT_FILTER', res.data.data)
            } catch (e) {
                const logger = this.vue.$logger('store-filter-fetchFilterById')
                logger.error(e)
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
                let res = await this.vue.$axios.post(`/filters`, body)
                let message = [`Filter ${payload.id} is created`]
                // response ok
                if (res.status === 204) {
                    commit(
                        'SET_SNACK_BAR_MESSAGE',
                        {
                            text: message,
                            type: 'success',
                        },
                        { root: true }
                    )
                    if (this.vue.$help.isFunction(payload.callback)) await payload.callback()
                }
            } catch (e) {
                const logger = this.vue.$logger('store-filter-createFilter')
                logger.error(e)
            }
        },

        async destroyFilter({ dispatch, commit }, id) {
            try {
                let res = await this.vue.$axios.delete(`/filters/${id}?force=yes`)
                if (res.status === 204) {
                    await dispatch('fetchAllFilters')
                    commit(
                        'SET_SNACK_BAR_MESSAGE',
                        {
                            text: [`Filter ${id} is destroyed`],
                            type: 'success',
                        },
                        { root: true }
                    )
                }
            } catch (e) {
                const logger = this.vue.$logger('store-filter-destroyFilter')
                logger.error(e)
            }
        },
    },
    getters: {
        // -------------- below getters are available only when fetchAllFilters has been dispatched
        getAllFiltersMap: state => {
            let map = new Map()
            state.all_filters.forEach(ele => {
                map.set(ele.id, ele)
            })
            return map
        },

        getAllFiltersInfo: state => {
            let idArr = []
            return state.all_filters.reduce((accumulator, _, index, array) => {
                idArr.push(array[index].id)
                return (accumulator = { idArr: idArr })
            }, [])
        },
    },
}
