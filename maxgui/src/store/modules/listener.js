/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { genSetMutations } from '@share/utils/helpers'

const states = () => ({
    all_listeners: [],
    current_listener: {},
})
export default {
    namespaced: true,
    state: states(),
    mutations: genSetMutations(states()),
    actions: {
        async fetchAllListeners({ commit }) {
            try {
                let res = await this.vue.$http.get(`/listeners`)
                if (res.data.data) commit('SET_ALL_LISTENERS', res.data.data)
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },

        async fetchListenerById({ commit }, id) {
            try {
                let res = await this.vue.$http.get(`/listeners/${id}`)
                if (res.data.data) commit('SET_CURRENT_LISTENER', res.data.data)
            } catch (e) {
                this.vue.$logger.error(e)
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

                let res = await this.vue.$http.post(`/listeners`, body)
                let message = [`Listener ${payload.id} is created`]
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
         * @param {String} payload.id Name of the listener
         * @param {Object} payload.parameters Parameters for the listener
         * @param {Function} payload.callback callback function after successfully updated
         */
        async updateListenerParameters({ commit }, payload) {
            try {
                const body = {
                    data: {
                        id: payload.id,
                        type: 'listeners',
                        attributes: { parameters: payload.parameters },
                    },
                }
                let res = await this.vue.$http.patch(`/listeners/${payload.id}`, body)
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
        async destroyListener({ dispatch, commit }, id) {
            try {
                let res = await this.vue.$http.delete(`/listeners/${id}`)
                if (res.status === 204) {
                    await dispatch('fetchAllListeners')
                    commit(
                        'mxsApp/SET_SNACK_BAR_MESSAGE',
                        {
                            text: [`Listeners ${id} is destroyed`],
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
        // -------------- below getters are available only when fetchAllListeners has been dispatched
        getTotalListeners: state => state.all_listeners.length,
        getAllListenersMap: state => {
            let map = new Map()
            state.all_listeners.forEach(ele => {
                map.set(ele.id, ele)
            })
            return map
        },
    },
}
