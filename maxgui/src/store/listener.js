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
        all_listeners: [],
        current_listener: {},
    },
    mutations: {
        SET_ALL_LISTENERS(state, payload) {
            state.all_listeners = payload
        },
        SET_CURRENT_LISTENER(state, payload) {
            state.current_listener = payload
        },
    },
    actions: {
        async fetchAllListeners({ commit }) {
            try {
                let res = await this.vue.$axios.get(`/listeners`)
                if (res.data.data) commit('SET_ALL_LISTENERS', res.data.data)
            } catch (e) {
                const logger = this.vue.$logger('store-listener-fetchAllListeners')
                logger.error(e)
            }
        },

        async fetchListenerById({ commit }, id) {
            try {
                let res = await this.vue.$axios.get(`/listeners/${id}`)
                if (res.data.data) commit('SET_CURRENT_LISTENER', res.data.data)
            } catch (e) {
                const logger = this.vue.$logger('store-listener-fetchListenerById')
                logger.error(e)
            }
        },
        /**
         * @param {Object} payload payload object for creating listener
         * @param {String} payload.id Name of the listener
         * @param {Object} payload.parameters listener parameters
         * @param {Object} payload.relationships feed a service
         * @param {Function} payload.callback callback function after successfully updated
         */
        async createListener({ commit }, payload) {
            try {
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

                let res = await this.vue.$axios.post(`/listeners`, body)
                let message = [`Listener ${payload.id} is created`]
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
                const logger = this.vue.$logger('store-listener-createListener')
                logger.error(e)
            }
        },

        async destroyListener({ dispatch, commit }, id) {
            try {
                let res = await this.vue.$axios.delete(`/listeners/${id}`)
                if (res.status === 204) {
                    await dispatch('fetchAllListeners')
                    commit(
                        'SET_SNACK_BAR_MESSAGE',
                        {
                            text: [`Listeners ${id} is destroyed`],
                            type: 'success',
                        },
                        { root: true }
                    )
                }
            } catch (e) {
                const logger = this.vue.$logger('store-listener-destroyListener')
                logger.error(e)
            }
        },
    },
    getters: {
        // -------------- below getters are available only when fetchAllListeners has been dispatched
        getAllListenersMap: state => {
            let map = new Map()
            state.all_listeners.forEach(ele => {
                map.set(ele.id, ele)
            })
            return map
        },

        getAllListenersInfo: state => {
            let idArr = []
            return state.all_listeners.reduce((accumulator, _, index, array) => {
                idArr.push(array[index].id)
                return (accumulator = { idArr: idArr })
            }, [])
        },
    },
}
