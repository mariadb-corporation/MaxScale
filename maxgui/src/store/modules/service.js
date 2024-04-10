/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { genSetMutations } from '@share/utils/helpers'

const states = () => ({
    all_services: [],
    current_service: {},
    service_connections_datasets: [],
})

export default {
    namespaced: true,
    state: states(),
    mutations: genSetMutations(states()),
    actions: {
        async fetchServiceById({ commit }, id) {
            try {
                let res = await this.vue.$http.get(`/services/${id}`)
                if (res.data.data) {
                    commit('SET_CURRENT_SERVICE', res.data.data)
                }
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },
        genDataSets({ commit, state }) {
            const {
                current_service: { attributes: { connections = null } = {} },
            } = state
            if (connections !== null) {
                const { genLineStreamDataset } = this.vue.$helpers
                const dataset = genLineStreamDataset({
                    label: 'Current connections',
                    value: connections,
                    colorIndex: 0,
                })
                commit('SET_SERVICE_CONNECTIONS_DATASETS', [dataset])
            }
        },

        async fetchAllServices({ commit }) {
            try {
                let res = await this.vue.$http.get(`/services`)
                if (res.data.data) commit('SET_ALL_SERVICES', res.data.data)
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },

        //-----------------------------------------------Service Create/Update/Delete----------------------------------
        /**
         * @param {Object} payload payload object
         * @param {String} payload.id Name of the service
         * @param {String} payload.module The router module to use
         * @param {Object} payload.parameters Parameters for the service
         * @param {Object} payload.relationships The relationships of the service to other resources
         * @param {Object} payload.relationships.servers servers object
         * @param {Object} payload.relationships.filters filters object
         * @param {Function} payload.callback callback function after successfully updated
         */
        async createService({ commit }, payload) {
            try {
                const body = {
                    data: {
                        id: payload.id,
                        type: 'services',
                        attributes: {
                            router: payload.module,
                            parameters: payload.parameters,
                        },
                        relationships: payload.relationships,
                    },
                }
                let res = await this.vue.$http.post(`/services/`, body)

                // response ok
                if (res.status === 204) {
                    commit(
                        'mxsApp/SET_SNACK_BAR_MESSAGE',
                        {
                            text: [`Service ${payload.id} is created`],
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
        //-----------------------------------------------Service parameter update---------------------------------
        /**
         * @param {Object} payload payload object
         * @param {String} payload.id Name of the service
         * @param {Object} payload.parameters Parameters for the service
         * @param {Function} payload.callback callback function after successfully updated
         */
        async updateServiceParameters({ commit }, payload) {
            try {
                const body = {
                    data: {
                        id: payload.id,
                        type: 'services',
                        attributes: { parameters: payload.parameters },
                    },
                }
                let res = await this.vue.$http.patch(`/services/${payload.id}`, body)
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

        //-----------------------------------------------Service relationship update---------------------------------
        /**
         * @param {String} payload.id Name of the service
         * @param {Array} payload.data servers||filters||services||monitors array
         * @param {Object} payload.type Type of relationships
         * @param {Function} payload.callback callback function after successfully updated
         */
        async updateServiceRelationship({ commit }, { id, data, type, callback }) {
            try {
                let res, message
                res = await this.vue.$http.patch(`/services/${id}/relationships/${type}`, { data })
                message = [`Update ${id} relationships successfully`]
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
                    await this.vue.$typy(callback).safeFunction()
                }
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },

        async destroyService({ dispatch, commit }, id) {
            try {
                let res = await this.vue.$http.delete(`/services/${id}?force=yes`)
                // response ok
                if (res.status === 204) {
                    await dispatch('fetchAllServices')
                    commit(
                        'mxsApp/SET_SNACK_BAR_MESSAGE',
                        {
                            text: [`Service ${id} is deleted`],
                            type: 'success',
                        },
                        { root: true }
                    )
                }
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },

        /**
         * @param {Object} param - An object.
         * @param {String} param.id id of the service
         * @param {String} param.mode Mode to start or stop service
         * @param {Function} param.callback callback function after successfully updated
         */
        async stopOrStartService({ commit }, { id, mode, callback }) {
            try {
                let res = await this.vue.$http.put(`/services/${id}/${mode}`)
                let message
                switch (mode) {
                    case 'start':
                        message = [`Service ${id} is started`]
                        break
                    case 'stop':
                        message = [`Service${id} is stopped`]
                        break
                }
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
                    await this.vue.$typy(callback).safeFunction()
                }
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },
    },
    getters: {
        // -------------- below getters are available only when fetchAllServices has been dispatched
        getTotalServices: state => state.all_services.length,
    },
}
