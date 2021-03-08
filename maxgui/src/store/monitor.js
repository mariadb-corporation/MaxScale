/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    namespaced: true,
    state: {
        all_monitors: [],
        current_monitor: {},
        monitor_diagnostics: {},
    },
    mutations: {
        SET_ALL_MONITORS(state, payload) {
            state.all_monitors = payload
        },
        SET_CURRENT_MONITOR(state, payload) {
            state.current_monitor = payload
        },
        SET_MONITOR_DIAGNOSTICS(state, payload) {
            state.monitor_diagnostics = payload
        },
    },
    actions: {
        async fetchAllMonitors({ commit }) {
            try {
                let res = await this.vue.$axios.get(`/monitors`)
                if (res.data.data) commit('SET_ALL_MONITORS', res.data.data)
            } catch (e) {
                const logger = this.vue.$logger('store-monitor-fetchAllMonitors')
                logger.error(e)
            }
        },
        async fetchMonitorById({ commit }, id) {
            try {
                let res = await this.vue.$axios.get(`/monitors/${id}`)
                if (res.data.data) commit('SET_CURRENT_MONITOR', res.data.data)
            } catch (e) {
                const logger = this.vue.$logger('store-monitor-fetchMonitorById')
                logger.error(e)
            }
        },
        async fetchMonitorDiagnosticsById({ commit }, id) {
            try {
                let res = await this.vue.$axios.get(
                    `/monitors/${id}?fields[monitors]=monitor_diagnostics`
                )
                if (res.data.data) commit('SET_MONITOR_DIAGNOSTICS', res.data.data)
            } catch (e) {
                const logger = this.vue.$logger('store-monitor-fetchMonitorDiagnosticsById')
                logger.error(e)
            }
        },
        /**
         * @param {Object} payload payload object
         * @param {String} payload.id Name of the monitor
         * @param {String} payload.module The module to use
         * @param {Object} payload.parameters Parameters for the monitor
         * @param {Object} payload.relationships The relationships of the monitor to other resources
         * @param {Object} payload.relationships.servers severs relationships
         * @param {Function} payload.callback callback function after successfully updated
         */
        async createMonitor({ commit }, payload) {
            try {
                const body = {
                    data: {
                        id: payload.id,
                        type: 'monitors',
                        attributes: {
                            module: payload.module,
                            parameters: payload.parameters,
                        },
                        relationships: payload.relationships,
                    },
                }
                let res = await this.vue.$axios.post(`/monitors/`, body)
                let message = [`Monitor ${payload.id} is created`]
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
                const logger = this.vue.$logger('store-monitor-createMonitor')
                logger.error(e)
            }
        },

        //-----------------------------------------------Monitor parameter update---------------------------------
        /**
         * @param {Object} payload payload object
         * @param {String} payload.id Name of the monitor
         * @param {Object} payload.parameters Parameters for the monitor
         * @param {Function} payload.callback callback function after successfully updated
         */
        async updateMonitorParameters({ commit }, payload) {
            try {
                const body = {
                    data: {
                        id: payload.id,
                        type: 'monitors',
                        attributes: { parameters: payload.parameters },
                    },
                }
                let res = await this.vue.$axios.patch(`/monitors/${payload.id}`, body)
                // response ok
                if (res.status === 204) {
                    commit(
                        'SET_SNACK_BAR_MESSAGE',
                        {
                            text: [`Monitor ${payload.id} is updated`],
                            type: 'success',
                        },
                        { root: true }
                    )
                    if (this.vue.$help.isFunction(payload.callback)) await payload.callback()
                }
            } catch (e) {
                const logger = this.vue.$logger('store-monitor-updateMonitorParameters')
                logger.error(e)
            }
        },
        /**
         * @param {String} id id of the monitor to be manipulated
         * @param {String} mode Mode to manipulate the monitor ( destroy, stop, start)
         * @param {Function} callback callback function after successfully updated
         */
        async manipulateMonitor({ commit }, { id, mode, callback }) {
            try {
                let res, message
                switch (mode) {
                    case 'destroy':
                        /*  Destroy a created monitor.
                        The monitor must not have relationships to any servers in order to be destroyed. */
                        res = await this.vue.$axios.delete(`/monitors/${id}?force=yes`)
                        message = [`Monitor ${id} is destroyed`]
                        break
                    case 'stop':
                        //Stops a started monitor.
                        res = await this.vue.$axios.put(`/monitors/${id}/stop`)
                        message = [`Monitor ${id} is stopped`]
                        break
                    case 'start':
                        //Starts a stopped monitor.
                        res = await this.vue.$axios.put(`/monitors/${id}/start`)
                        message = [`Monitor ${id} is started`]
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
                const logger = this.vue.$logger('store-monitor-manipulateMonitor')
                logger.error(e)
            }
        },

        //-----------------------------------------------Monitor relationship update---------------------------------
        /**
         * @param {Object} payload payload object
         * @param {String} payload.id Name of the monitor
         * @param {Array} payload.servers servers array
         * @param {Function} payload.callback callback function after successfully updated
         */
        async updateMonitorRelationship({ commit }, payload) {
            try {
                let res
                let message

                res = await this.vue.$axios.patch(`/monitors/${payload.id}/relationships/servers`, {
                    data: payload.servers,
                })
                message = [`Servers relationships of ${payload.id} is updated`]

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
                const logger = this.vue.$logger('store-monitor-updateMonitorRelationship')
                logger.error(e)
            }
        },

        /**
         * @param {String} payload.monitorModule Monitor module
         * @param {String} payload.monitorId Monitor id
         * @param {String} payload.master Name of the new master server
         * @param {Function} payload.callback callback function after successfully updated
         */
        async switchOver(_, { monitorModule, monitorId, masterId, callback }) {
            try {
                let res
                res = await this.vue.$axios.post(
                    `/maxscale/modules/${monitorModule}/async-switchover?${monitorId}&${masterId}`
                )
                // response ok
                if (res.status === 204) if (this.vue.$help.isFunction(callback)) await callback()
            } catch (e) {
                const logger = this.vue.$logger('store-monitor-switchOver')
                logger.error(e)
            }
        },

        /**
         * @param {String} payload.monitorModule Monitor module
         * @param {String} payload.monitorId Monitor id
         * @return {Object} response object
         */
        async fetchAsyncResults(_, { monitorModule, monitorId }) {
            try {
                return await this.vue.$axios.get(
                    `/maxscale/modules/${monitorModule}/fetch-cmd-results?${monitorId}`
                )
            } catch (e) {
                this.vue.$logger('store-monitor-fetchAsyncResults').error(e)
            }
        },
    },
    getters: {
        // -------------- below getters are available only when fetchAllMonitors has been dispatched
        getAllMonitorsMap: state => {
            let map = new Map()
            state.all_monitors.forEach(ele => {
                map.set(ele.id, ele)
            })
            return map
        },

        getAllMonitorsInfo: state => {
            let idArr = []
            return state.all_monitors.reduce((accumulator, _, index, array) => {
                idArr.push(array[index].id)
                return (accumulator = { idArr: idArr })
            }, [])
        },
    },
}
