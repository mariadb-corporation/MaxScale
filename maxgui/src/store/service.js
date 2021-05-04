/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-04-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    namespaced: true,
    state: {
        all_services: [],
        current_service: {},
        service_connections_datasets: [],
        service_connection_info: {},
    },
    mutations: {
        SET_ALL_SERVICES(state, payload) {
            state.all_services = payload
        },
        SET_CURRENT_SERVICE(state, payload) {
            state.current_service = payload
        },
        SET_SERVICE_CONNECTIONS_DATASETS(state, payload) {
            state.service_connections_datasets = payload
        },
        SET_SERVICE_CONNECTIONS_INFO(state, payload) {
            state.service_connection_info = payload
        },
    },
    actions: {
        async fetchServiceById({ commit }, id) {
            try {
                let res = await this.vue.$axios.get(`/services/${id}`)
                if (res.data.data) {
                    commit('SET_CURRENT_SERVICE', res.data.data)
                }
            } catch (e) {
                const logger = this.vue.$logger('store-service-fetchServiceById')
                logger.error(e)
            }
        },

        genDataSets({ commit, state }) {
            const {
                current_service: { attributes: { connections = null } = {} },
            } = state
            if (connections !== null) {
                const { genLineDataSet } = this.vue.$help
                const dataset = genLineDataSet({
                    label: 'Current connections',
                    value: connections,
                    colorIndex: 0,
                })
                commit('SET_SERVICE_CONNECTIONS_DATASETS', [dataset])
            }
        },

        async fetchAllServices({ commit }) {
            try {
                let res = await this.vue.$axios.get(`/services`)
                if (res.data.data) commit('SET_ALL_SERVICES', res.data.data)
            } catch (e) {
                const logger = this.vue.$logger('store-services-fetchAllServices')
                logger.error(e)
            }
        },

        async fetchServiceConnections({ commit }, id) {
            try {
                let res = await this.vue.$axios.get(
                    `/services/${id}?fields[services]=connections,total_connections`
                )
                if (res.data.data) {
                    let { attributes: { connections, total_connections } = {} } = res.data.data
                    commit('SET_SERVICE_CONNECTIONS_INFO', {
                        total_connections: total_connections,
                        connections: connections,
                    })
                }
            } catch (e) {
                const logger = this.vue.$logger('store-service-fetchServiceConnections')
                logger.error(e)
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
                let res = await this.vue.$axios.post(`/services/`, body)

                // response ok
                if (res.status === 204) {
                    commit(
                        'SET_SNACK_BAR_MESSAGE',
                        {
                            text: [`Service ${payload.id} is created`],
                            type: 'success',
                        },
                        { root: true }
                    )
                    if (this.vue.$help.isFunction(payload.callback)) await payload.callback()
                }
            } catch (e) {
                const logger = this.vue.$logger('store-service-createService')
                logger.error(e)
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
                let res = await this.vue.$axios.patch(`/services/${payload.id}`, body)
                // response ok
                if (res.status === 204) {
                    commit(
                        'SET_SNACK_BAR_MESSAGE',
                        {
                            text: [`Parameters of ${payload.id} is updated`],
                            type: 'success',
                        },
                        { root: true }
                    )
                    if (this.vue.$help.isFunction(payload.callback)) await payload.callback()
                }
            } catch (e) {
                const logger = this.vue.$logger('store-service-updateServiceParameters')
                logger.error(e)
            }
        },

        //-----------------------------------------------Service relationship update---------------------------------
        /**
         * @param {Object} payload payload object
         * @param {String} payload.id Name of the service
         * @param {Array} payload.servers servers array
         * @param {Array} payload.filters filters array
         * @param {Object} payload.type Type of relationships
         * @param {Function} payload.callback callback function after successfully updated
         */
        async updateServiceRelationship({ commit }, payload) {
            try {
                let res
                let message

                res = await this.vue.$axios.patch(
                    `/services/${payload.id}/relationships/${payload.type}`,
                    {
                        data: payload.type === 'servers' ? payload.servers : payload.filters,
                    }
                )
                message = [
                    `${this.vue.$help.capitalizeFirstLetter(payload.type)} relationships of ${
                        payload.id
                    } is updated`,
                ]

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
                const logger = this.vue.$logger('store-service-updateServiceRelationship')
                logger.error(e)
            }
        },

        async destroyService({ dispatch, commit }, id) {
            try {
                let res = await this.vue.$axios.delete(`/services/${id}?force=yes`)
                // response ok
                if (res.status === 204) {
                    await dispatch('fetchAllServices')
                    commit(
                        'SET_SNACK_BAR_MESSAGE',
                        {
                            text: [`Service ${id} is deleted`],
                            type: 'success',
                        },
                        { root: true }
                    )
                }
            } catch (e) {
                const logger = this.vue.$logger('store-service-destroyService')
                logger.error(e)
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
                let res = await this.vue.$axios.put(`/services/${id}/${mode}`)
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
                        'SET_SNACK_BAR_MESSAGE',
                        {
                            text: message,
                            type: 'success',
                        },
                        { root: true }
                    )
                    if (this.vue.$help.isFunction(callback)) await callback()
                }
            } catch (e) {
                const logger = this.vue.$logger('store-service-stopOrStartService')
                logger.error(e)
            }
        },
    },
    getters: {
        // -------------- below getters are available only when fetchAllServices has been dispatched
        getAllServicesMap: state => {
            let map = new Map()
            state.all_services.forEach(ele => {
                map.set(ele.id, ele)
            })
            return map
        },

        getAllServicesInfo: state => {
            let idArr = []
            return state.all_services.reduce((accumulator, _, index, array) => {
                idArr.push(array[index].id)
                return (accumulator = { idArr: idArr })
            }, [])
        },
    },
}
