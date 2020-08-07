/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-07-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    namespaced: true,
    state: {
        allFilters: [],
        currentFilter: {},
    },
    mutations: {
        setAllFilters(state, payload) {
            state.allFilters = payload
        },
        setCurrentFilter(state, payload) {
            state.currentFilter = payload
        },
    },
    actions: {
        async fetchAllFilters({ commit }) {
            try {
                let res = await this.vue.$axios.get(`/filters`)
                if (res.data.data) commit('setAllFilters', res.data.data)
            } catch (e) {
                if (process.env.NODE_ENV !== 'test') {
                    const logger = this.vue.$logger('store-filter-fetchAllFilters')
                    logger.error(e)
                }
            }
        },

        async fetchFilterById({ commit }, id) {
            try {
                let res = await this.vue.$axios.get(`/filters/${id}`)
                if (res.data.data) commit('setCurrentFilter', res.data.data)
            } catch (e) {
                if (process.env.NODE_ENV !== 'test') {
                    const logger = this.vue.$logger('store-filter-fetchFilterById')
                    logger.error(e)
                }
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
                        'showMessage',
                        {
                            text: message,
                            type: 'success',
                        },
                        { root: true }
                    )
                    if (this.vue.$help.isFunction(payload.callback)) await payload.callback()
                }
            } catch (e) {
                if (process.env.NODE_ENV !== 'test') {
                    const logger = this.vue.$logger('store-filter-createFilter')
                    logger.error(e)
                }
            }
        },
        /**
         * @param {String} object.id Name of the filter to be destroyed
         */
        async destroyFilter({ dispatch, commit }, id) {
            try {
                let res = await this.vue.$axios.delete(`/filters/${id}?force=yes`)
                if (res.status === 204) {
                    await dispatch('fetchAllFilters')
                    commit(
                        'showMessage',
                        {
                            text: [`Filter ${id} is destroyed`],
                            type: 'success',
                        },
                        { root: true }
                    )
                }
            } catch (e) {
                if (process.env.NODE_ENV !== 'test') {
                    const logger = this.vue.$logger('store-filter-destroyFilter')
                    logger.error(e)
                }
            }
        },
    },
    getters: {
        allFilters: state => state.allFilters,
        currentFilter: state => state.currentFilter,
        // -------------- below getters are available only when fetchAllFilters has been dispatched
        allFiltersMap: state => {
            let map = new Map()
            state.allFilters.forEach(ele => {
                map.set(ele.id, ele)
            })
            return map
        },

        allFiltersInfo: state => {
            let idArr = []
            return state.allFilters.reduce((accumulator, _, index, array) => {
                idArr.push(array[index].id)
                return (accumulator = { idArr: idArr })
            }, [])
        },
    },
}
