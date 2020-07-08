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
        allServers: [],
        currentServer: {},
        serversConnectionsChartData: {
            datasets: [],
        },
    },
    mutations: {
        /**
         * @param {Array} payload // List of server resources
         */
        setServers(state, payload) {
            state.allServers = payload
        },
        setCurrentServer(state, payload) {
            state.currentServer = payload
        },
        setServersConnectionsChartData(state, payload) {
            state.serversConnectionsChartData = payload
        },
    },
    actions: {
        async fetchAllServers({ commit }) {
            let res = await this.Vue.axios.get(`/servers`)
            // reverse array, latest will be last
            let sorted = res.data.data.reverse()
            commit('setServers', sorted)
        },
        async fetchServerById({ commit, state }, id) {
            let res = await this.Vue.axios.get(`/servers/${id}`, {
                auth: state.credentials,
            })
            commit('setCurrentServer', res.data.data)
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
            let res = await this.Vue.axios.post(`/servers/`, body)
            let message = [`Server ${payload.id} is created`]
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
        //-----------------------------------------------Server parameter update---------------------------------
        /**
         * @param {Object} payload payload object
         * @param {String} payload.id Name of the server
         * @param {Object} payload.parameters Parameters for the server
         * @param {Object} payload.callback callback function after successfully updated
         */
        async updateServerParameters({ commit }, payload) {
            const body = {
                data: {
                    id: payload.id,
                    type: 'servers',
                    attributes: { parameters: payload.parameters },
                },
            }
            let res = await this.Vue.axios.patch(`/servers/${payload.id}`, body)
            // response ok
            if (res.status === 204) {
                commit(
                    'showMessage',
                    {
                        text: [`Parameters of ${payload.id} is updated`],
                        type: 'success',
                    },
                    { root: true }
                )
                if (this.Vue.prototype.$help.isFunction(payload.callback)) await payload.callback()
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
            let res
            let message

            res = await this.Vue.axios.patch(
                `/servers/${payload.id}/relationships/${payload.type}`,
                {
                    data: payload.type === 'services' ? payload.services : payload.monitors,
                }
            )

            message = [
                `${this.Vue.prototype.$help.capitalizeFirstLetter(payload.type)} relationships of ${
                    payload.id
                } is updated`,
            ]

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
         * @param {String} id id of the server
         */
        async destroyServer({ dispatch, commit }, id) {
            let res = await this.Vue.axios.delete(`/servers/${id}`)
            // response ok
            if (res.status === 204) {
                await dispatch('fetchAllServers')
                commit(
                    'showMessage',
                    {
                        text: [`Server ${id} is deleted`],
                        type: 'success',
                    },
                    { root: true }
                )
            }
        },
        /**
         * @param {String} id id of the server
         * @param {String} state state of the server maintenance or drain)
         * @param {String} mode mode set or clear
         * @param {Boolean} forceClosing force all connections to the server to be closed immediately. Only works
         * for maintenance mode
         */
        async setOrClearServerState({ commit }, { id, state, mode, callback, forceClosing }) {
            let res, message

            switch (mode) {
                case 'set':
                    {
                        let url = `/servers/${id}/set?state=${state}`
                        if (state === 'maintenance' && forceClosing) url = url.concat('&force=yes')
                        res = await this.Vue.axios.put(url)
                        message = [`Server ${id} is set to ${state}`]
                    }
                    break
                case 'clear':
                    res = await this.Vue.axios.put(`/servers/${id}/clear?state=${state}`)
                    message = [`State ${state} of server ${id} is cleared`]
                    break
            }
            // response ok
            if (res.status === 204) {
                commit('showMessage', { text: message, type: 'success' }, { root: true })
                if (this.Vue.prototype.$help.isFunction(callback)) await callback()
            }
        },

        /**
        Generate data schema for total connections of each server
         */
        genDataSetSchema({ commit, state }) {
            const { allServers } = state
            if (allServers.length) {
                let arr = []
                let lineColors = []
                for (let i = 0; i < allServers.length; ++i) {
                    const {
                        id,
                        attributes: { statistics },
                    } = allServers[i]
                    lineColors.push(this.Vue.prototype.$help.dynamicColors(i))
                    let indexOfOpacity = lineColors[i].lastIndexOf(')') - 1
                    let obj = {
                        label: `Server ID - ${id}`,
                        id: `Server ID - ${id}`,
                        type: 'line',
                        // background of the line
                        backgroundColor: this.Vue.prototype.$help.strReplaceAt(
                            lineColors[i],
                            indexOfOpacity,
                            '0.2'
                        ),
                        borderColor: lineColors[i], //theme.palette.primary.main, // line color
                        borderWidth: 1,
                        lineTension: 0,
                        data: [{ x: Date.now(), y: statistics.connections }],
                    }
                    arr.push(obj)
                }
                let serversConnectionsChartDataSchema = {
                    datasets: arr,
                }
                commit('setServersConnectionsChartData', serversConnectionsChartDataSchema)
            }
        },
    },
    getters: {
        allServers: state => state.allServers,
        currentServer: state => state.currentServer,
        serversConnectionsChartData: state => state.serversConnectionsChartData,
        // -------------- below getters are available only when fetchAllServers has been dispatched
        allServersMap: state => {
            let map = new Map()
            state.allServers.forEach(ele => {
                map.set(ele.id, ele)
            })
            return map
        },

        allServersInfo: state => {
            let idArr = []
            let portNumArr = []
            return state.allServers.reduce((accumulator, _, index, array) => {
                idArr.push(array[index].id)
                portNumArr.push(array[index].attributes.parameters.port)

                return (accumulator = { idArr: idArr, portNumArr: portNumArr })
            }, [])
        },
    },
}
