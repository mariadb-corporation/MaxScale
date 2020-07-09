/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    namespaced: true,
    state: {
        allFilters: [],
    },
    mutations: {
        /**
         * @param {Array} payload  // Array of filter resources
         */
        setAllFilters(state, payload) {
            state.allFilters = payload
        },
    },
    actions: {
        async fetchAllFilters({ commit }) {
            let res = await this.Vue.axios.get(`/filters`)
            commit('setAllFilters', res.data.data)
        },

        /**
         * @param {Object} payload payload object for creating filter
         * @param {String} payload.id Name of the filter
         * @param {String} payload.module The filter module to use
         * @param {Object} payload.parameters Parameters for the filter
         * @param {Function} payload.callback callback function after successfully updated
         */
        async createFilter({ commit }, payload) {
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
            let res = await this.Vue.axios.post(`/filters`, body)
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
                if (this.Vue.prototype.$help.isFunction(payload.callback)) await payload.callback()
            }
        },
        /**
         * @param {String} object.id Name of the filter to be destroyed,
         * A filter can only be destroyed if no service uses it.
         * This means that the data.relationships object for the filter must be empty.
         * Note that the service → filter relationship cannot be modified from the filters resource and must
         * be done via the services resource.
         */
        async destroyFilter({ dispatch, commit }, id) {
            let res = await this.Vue.axios.delete(`/filters/${id}`)
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
        },
    },
    getters: {
        allFilters: state => state.allFilters,
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
