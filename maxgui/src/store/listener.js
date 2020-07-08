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
        allListeners: [],
    },
    mutations: {
        /**
         * @param {Array} payload  // Array of listeners resources
         */
        setAllListeners(state, payload) {
            state.allListeners = payload
        },
    },
    actions: {
        async fetchAllListeners({ commit }) {
            let res = await this.Vue.axios.get(`/listeners`)
            commit('setAllListeners', res.data.data)
        },

        /**
         * @param {Object} payload payload object for creating listener
         * @param {String} payload.id Name of the listener
         * @param {Object} payload.parameters listener parameters
         * @param {Object} payload.relationships feed a service
         * @param {Function} payload.callback callback function after successfully updated
         */
        async createListener({ commit }, payload) {
            const body = {
                data: {
                    id: payload.id,
                    type: 'listeners',
                    attributes: {
                        parameters: payload.parameters,
                    },
                    relationships: payload.relationships,
                },
            }

            let res = await this.Vue.axios.post(`/listeners`, body)
            let message = [`Listener ${payload.id} is created`]
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
        async destroyListener({ dispatch, commit }, id) {
            let res = await this.Vue.axios.delete(`/listeners/${id}`)
            if (res.status === 204) {
                await dispatch('fetchAllListeners')
                commit(
                    'showMessage',
                    {
                        text: [`Listeners ${id} is destroyed`],
                        type: 'success',
                    },
                    { root: true }
                )
            }
        },
    },
    getters: {
        allListeners: state => state.allListeners,
        // -------------- below getters are available only when fetchAllListeners has been dispatched
        allListenersMap: state => {
            let map = new Map()
            state.allListeners.forEach(ele => {
                map.set(ele.id, ele)
            })
            return map
        },

        allListenersInfo: state => {
            let idArr = []
            return state.allListeners.reduce((accumulator, _, index, array) => {
                idArr.push(array[index].id)
                return (accumulator = { idArr: idArr })
            }, [])
        },
    },
}
