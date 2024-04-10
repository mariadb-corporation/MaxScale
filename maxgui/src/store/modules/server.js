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
import { SERVER_OP_TYPES } from '@src/constants'
import { genSetMutations } from '@share/utils/helpers'

const states = () => ({
    all_servers: [],
    all_server_names: [],
    current_server: {},
    server_connections_datasets: [],
})

export default {
    namespaced: true,
    state: states(),
    mutations: genSetMutations(states()),
    actions: {
        async fetchAllServers({ commit }) {
            try {
                let res = await this.vue.$http.get(`/servers`)
                if (res.data.data) commit('SET_ALL_SERVERS', res.data.data)
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },

        async fetchAllServerNames({ commit }) {
            try {
                let res = await this.vue.$http.get(`/servers?fields[servers]=name`)
                if (res.data.data)
                    commit(
                        'SET_ALL_SERVER_NAMES',
                        res.data.data.map(o => o.attributes.name)
                    )
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },

        async fetchServerById({ commit }, id) {
            try {
                let res = await this.vue.$http.get(`/servers/${id}`)
                if (res.data.data) commit('SET_CURRENT_SERVER', res.data.data)
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },
        //-----------------------------------------------Server Create/Update/Delete----------------------------------
        /**
         * @param {Object} payload payload object
         * @param {String} payload.id Name of the server
         * @param {Object} payload.parameters Parameters for the server
         * @param {Object} payload.relationships The relationships of the server to other resources
         * @param {Object} payload.relationships.services services object
         * @param {Object} payload.relationships.monitors monitors object
         * @param {Function} payload.callback callback function after successfully updated
         */
        async createServer({ commit }, payload) {
            try {
                const body = {
                    data: {
                        id: payload.id,
                        type: 'servers',
                        attributes: {
                            parameters: payload.parameters,
                        },
                        relationships: payload.relationships,
                    },
                }
                let res = await this.vue.$http.post(`/servers/`, body)
                let message = [`Server ${payload.id} is created`]
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

        //-----------------------------------------------Server parameter update---------------------------------
        /**
         * @param {Object} payload payload object
         * @param {String} payload.id Name of the server
         * @param {Object} payload.parameters Parameters for the server
         * @param {Function} payload.callback callback function after successfully updated
         */
        async updateServerParameters({ commit }, payload) {
            try {
                const body = {
                    data: {
                        id: payload.id,
                        type: 'servers',
                        attributes: { parameters: payload.parameters },
                    },
                }
                let res = await this.vue.$http.patch(`/servers/${payload.id}`, body)
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

        //-----------------------------------------------Server relationship update---------------------------------
        /**
         * @param {Object} payload payload object
         * @param {String} payload.id Name of the server
         * @param {Array} payload.services services array
         * @param {Array} payload.monitors monitors array
         * @param {Object} payload.type Type of relationships
         * @param {Function} payload.callback callback function after successfully updated
         */
        async updateServerRelationship({ commit }, payload) {
            try {
                let res
                let message

                res = await this.vue.$http.patch(
                    `/servers/${payload.id}/relationships/${payload.type}`,
                    {
                        data: payload.type === 'services' ? payload.services : payload.monitors,
                    }
                )

                message = [
                    `${this.vue.$helpers.capitalizeFirstLetter(payload.type)} relationships of ${
                        payload.id
                    } is updated`,
                ]

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

        async destroyServer({ commit }, id) {
            try {
                let res = await this.vue.$http.delete(`/servers/${id}?force=yes`)
                // response ok
                if (res.status === 204) {
                    commit(
                        'mxsApp/SET_SNACK_BAR_MESSAGE',
                        {
                            text: [`Server ${id} is deleted`],
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
         * @param {String} param.id - id of the server
         * @param {String} param.type - type of operation: drain, clear, maintain
         * @param {String} param.opParams - operation params
         * @param {Function} param.callback - callback function after successfully updated
         * @param {Boolean} param.forceClosing - Immediately closing all connections to the server (maintain type)
         */
        async setOrClearServerState({ commit }, { id, type, opParams, callback, forceClosing }) {
            try {
                const nextStateMode = opParams.replace(/(clear|set)\?state=/, '')
                let message = [`Set ${id} to '${nextStateMode}'`]
                let url = `/servers/${id}/${opParams}`
                switch (type) {
                    case 'maintain':
                        if (forceClosing) url = url.concat('&force=yes')
                        break
                    case 'clear':
                        message = [`State '${nextStateMode}' of server ${id} is cleared`]
                        break
                }
                const res = await this.vue.$http.put(url)
                // response ok
                if (res.status === 204) {
                    commit(
                        'mxsApp/SET_SNACK_BAR_MESSAGE',
                        { text: message, type: 'success' },
                        { root: true }
                    )
                    await this.vue.$typy(callback).safeFunction()
                }
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },

        /**
         *  Generate data schema for total connections of each server
         */
        genDataSets({ commit, state }) {
            const { all_servers } = state
            const { genLineStreamDataset } = this.vue.$helpers

            if (all_servers.length) {
                let dataSets = []
                all_servers.forEach((server, i) => {
                    const {
                        id,
                        attributes: { statistics: { connections = null } = {} } = {},
                    } = server
                    if (connections !== null) {
                        const dataset = genLineStreamDataset({
                            label: `Server ID - ${id}`,
                            value: connections,
                            colorIndex: i,
                            id,
                        })
                        dataSets.push(dataset)
                    }
                })

                commit('SET_SERVER_CONNECTIONS_DATASETS', dataSets)
            }
        },
    },
    getters: {
        // -------------- below getters are available only when fetchAllServers has been dispatched
        getTotalServers: state => state.all_servers.length,
        getAllServersMap: state => {
            let map = new Map()
            state.all_servers.forEach(ele => {
                map.set(ele.id, ele)
            })
            return map
        },
        getCurrStateMode: () => {
            return serverState => {
                let currentState = serverState.toLowerCase()
                if (currentState.indexOf(',') > 0) {
                    currentState = currentState.slice(0, currentState.indexOf(','))
                }
                return currentState
            }
        },
        getServerOps: () => {
            const { MAINTAIN, CLEAR, DRAIN, DELETE } = SERVER_OP_TYPES
            // scope is needed to access $mxs_t
            return ({ currStateMode, scope }) => ({
                [MAINTAIN]: {
                    text: scope.$mxs_t('serverOps.actions.maintain'),
                    type: MAINTAIN,
                    icon: '$vuetify.icons.mxs_maintenance',
                    iconSize: 22,
                    color: 'primary',
                    info: scope.$mxs_t(`serverOps.info.maintain`),
                    params: 'set?state=maintenance',
                    disabled: currStateMode === 'maintenance',
                },
                [CLEAR]: {
                    text: scope.$mxs_t('serverOps.actions.clear'),
                    type: CLEAR,
                    icon: '$vuetify.icons.mxs_restart',
                    iconSize: 22,
                    color: 'primary',
                    info: '',
                    params: `clear?state=${currStateMode === 'drained' ? 'drain' : currStateMode}`,
                    disabled: currStateMode !== 'maintenance' && currStateMode !== 'drained',
                },
                [DRAIN]: {
                    text: scope.$mxs_t('serverOps.actions.drain'),
                    type: DRAIN,
                    icon: '$vuetify.icons.mxs_drain',
                    iconSize: 22,
                    color: 'primary',
                    info: scope.$mxs_t(`serverOps.info.drain`),
                    params: `set?state=drain`,
                    disabled: currStateMode === 'maintenance' || currStateMode === 'drained',
                },
                [DELETE]: {
                    text: scope.$mxs_t('serverOps.actions.delete'),
                    type: DELETE,
                    icon: '$vuetify.icons.mxs_delete',
                    iconSize: 18,
                    color: 'error',
                    info: '',
                    disabled: false,
                },
            })
        },
    },
}
