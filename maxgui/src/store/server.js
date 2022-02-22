/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    namespaced: true,
    state: {
        all_servers: [],
        current_server: {},
        current_server_stats: {},
        server_connections_datasets: [],
    },
    mutations: {
        /**
         * @param {Array} payload // List of server resources
         */
        SET_ALL_SERVERS(state, payload) {
            state.all_servers = payload
        },
        SET_CURRENT_SERVER(state, payload) {
            state.current_server = payload
        },
        SET_CURRENT_SERVER_STATS(state, payload) {
            state.current_server_stats = payload
        },
        SET_SERVER_CONNECTIONS_DATASETS(state, payload) {
            state.server_connections_datasets = payload
        },
    },
    actions: {
        async fetchAllServers({ commit }) {
            try {
                let res = await this.$http.get(`/servers`)
                if (res.data.data) {
                    let sorted = res.data.data
                    commit('SET_ALL_SERVERS', sorted)
                }
            } catch (e) {
                const logger = this.vue.$logger('store-server-fetchAllServers')
                logger.error(e)
            }
        },

        async fetchServerById({ commit }, id) {
            try {
                let res = await this.$http.get(`/servers/${id}`)
                if (res.data.data) commit('SET_CURRENT_SERVER', res.data.data)
            } catch (e) {
                const logger = this.vue.$logger('store-server-fetchServerById')
                logger.error(e)
            }
        },

        async fetchServerStatsById({ commit }, id) {
            try {
                const {
                    data: { data: { attributes: { statistics = null } = {} } = {} } = {},
                } = await this.$http.get(`/servers/${id}?fields[servers]=statistics`)
                if (statistics) commit('SET_CURRENT_SERVER_STATS', statistics)
            } catch (e) {
                const logger = this.vue.$logger('store-server-fetchServerStatsById')
                logger.error(e)
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
                let res = await this.$http.post(`/servers/`, body)
                let message = [`Server ${payload.id} is created`]
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
                const logger = this.vue.$logger('store-server-createServer')
                logger.error(e)
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
                let res = await this.$http.patch(`/servers/${payload.id}`, body)
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
                const logger = this.vue.$logger('store-server-updateServerParameters')
                logger.error(e)
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

                res = await this.$http.patch(
                    `/servers/${payload.id}/relationships/${payload.type}`,
                    {
                        data: payload.type === 'services' ? payload.services : payload.monitors,
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
                const logger = this.vue.$logger('store-server-updateServerRelationship')
                logger.error(e)
            }
        },

        async destroyServer({ commit }, id) {
            try {
                let res = await this.$http.delete(`/servers/${id}?force=yes`)
                // response ok
                if (res.status === 204) {
                    commit(
                        'SET_SNACK_BAR_MESSAGE',
                        {
                            text: [`Server ${id} is deleted`],
                            type: 'success',
                        },
                        { root: true }
                    )
                }
            } catch (e) {
                const logger = this.vue.$logger('store-server-destroyServer')
                logger.error(e)
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
                const res = await this.$http.put(url)
                // response ok
                if (res.status === 204) {
                    commit(
                        'SET_SNACK_BAR_MESSAGE',
                        { text: message, type: 'success' },
                        { root: true }
                    )
                    if (this.vue.$help.isFunction(callback)) await callback()
                }
            } catch (e) {
                const logger = this.vue.$logger('store-server-setOrClearServerState')
                logger.error(e)
            }
        },

        /**
         *  Generate data schema for total connections of each server
         */
        genDataSets({ commit, state }) {
            const { all_servers } = state
            const { genLineStreamDataset } = this.vue.$help

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
        getAllServersMap: state => {
            let map = new Map()
            state.all_servers.forEach(ele => {
                map.set(ele.id, ele)
            })
            return map
        },
        getAllServersInfo: state => {
            let idArr = []
            let portNumArr = []
            return state.all_servers.reduce((accumulator, _, index, array) => {
                idArr.push(array[index].id)
                portNumArr.push(array[index].attributes.parameters.port)

                return (accumulator = { idArr: idArr, portNumArr: portNumArr })
            }, [])
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
        getServerOps: (state, getters, rootState) => {
            const { MAINTAIN, CLEAR, DRAIN, DELETE } = rootState.app_config.SERVER_OP_TYPES
            // scope is needed to access $t
            return ({ currStateMode, scope }) => ({
                [MAINTAIN]: {
                    text: scope.$t('serverOps.actions.maintain'),
                    type: MAINTAIN,
                    icon: ' $vuetify.icons.paused',
                    iconSize: 22,
                    color: 'primary',
                    info: scope.$t(`serverOps.info.maintain`),
                    params: 'set?state=maintenance',
                    disabled: currStateMode === 'maintenance',
                },
                [CLEAR]: {
                    text: scope.$t('serverOps.actions.clear'),
                    type: CLEAR,
                    icon: '$vuetify.icons.restart',
                    iconSize: 22,
                    color: 'primary',
                    info: '',
                    params: `clear?state=${currStateMode === 'drained' ? 'drain' : currStateMode}`,
                    disabled: currStateMode !== 'maintenance' && currStateMode !== 'drained',
                },
                [DRAIN]: {
                    text: scope.$t('serverOps.actions.drain'),
                    type: DRAIN,
                    icon: '$vuetify.icons.drain',
                    iconSize: 22,
                    color: 'primary',
                    info: scope.$t(`serverOps.info.drain`),
                    params: `set?state=drain`,
                    disabled: currStateMode === 'maintenance' || currStateMode === 'drained',
                },
                [DELETE]: {
                    text: scope.$t('serverOps.actions.delete'),
                    type: DELETE,
                    icon: '$vuetify.icons.delete',
                    iconSize: 18,
                    color: 'error',
                    info: '',
                    disabled: false,
                },
            })
        },
    },
}
