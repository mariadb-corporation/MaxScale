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
function isSwitchingOver(msg) {
    let base = 'No manual command results are available, switchover is still'
    return msg === `${base} running.` || msg === `${base} pending.`
}
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
                let res = await this.$http.get(`/monitors`)
                if (res.data.data) commit('SET_ALL_MONITORS', res.data.data)
            } catch (e) {
                const logger = this.vue.$logger('store-monitor-fetchAllMonitors')
                logger.error(e)
            }
        },
        async fetchMonitorById({ commit }, id) {
            try {
                let res = await this.$http.get(`/monitors/${id}`)
                if (res.data.data) commit('SET_CURRENT_MONITOR', res.data.data)
            } catch (e) {
                const logger = this.vue.$logger('store-monitor-fetchMonitorById')
                logger.error(e)
            }
        },
        async fetchMonitorDiagnosticsById({ commit }, id) {
            try {
                let res = await this.$http.get(
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
                let res = await this.$http.post(`/monitors/`, body)
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
                let res = await this.$http.patch(`/monitors/${payload.id}`, body)
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
         * @param {String} param.id - id of the monitor to be manipulated
         * @param {String} param.type - type of operation: destroy, stop, start
         * @param {String|Object} param.opParams - operation params. Either a string or an object of params
         * @param {Function} param.callback callback function after successfully updated
         */
        async manipulateMonitor({ dispatch, commit, rootState }, { id, type, opParams, callback }) {
            try {
                let url = `/monitors/${id}/${opParams}`,
                    method = 'put',
                    message
                const { STOP, START, DESTROY, SWITCHOVER } = rootState.app_config.MONITOR_OP_TYPES
                switch (type) {
                    case DESTROY:
                        method = 'delete'
                        url = `/monitors/${id}?force=yes`
                        message = [`Monitor ${id} is destroyed`]
                        break
                    case STOP:
                        message = [`Monitor ${id} is stopped`]
                        break
                    case START:
                        message = [`Monitor ${id} is started`]
                        break
                    case SWITCHOVER: {
                        method = 'post'
                        const { moduleType, masterId } = opParams
                        url = `/maxscale/modules/${moduleType}/async-switchover?${id}&${masterId}`
                        break
                    }
                }
                const res = await this.$http[method](url)
                // response ok
                if (res.status === 204) {
                    switch (type) {
                        case SWITCHOVER:
                            await dispatch('checkSwitchOverRes', {
                                monitorModule: opParams.moduleType,
                                monitorId: id,
                                successCb: callback,
                            })
                            break
                        default:
                            commit(
                                'SET_SNACK_BAR_MESSAGE',
                                { text: message, type: 'success' },
                                { root: true }
                            )
                            if (this.vue.$help.isFunction(callback)) await callback()
                            break
                    }
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

                res = await this.$http.patch(`/monitors/${payload.id}/relationships/servers`, {
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
         * This function should be called right after switchover action is called
         * in order to show switchover status message on snackbar.
         * @param {String} payload.monitorModule Monitor module
         * @param {String} payload.monitorId Monitor id
         * @param {Function} successCb - callback function after successfully switchover
         */
        async checkSwitchOverRes({ dispatch }, { monitorModule, monitorId, successCb }) {
            try {
                const { status, data: { meta } = {} } = await this.$http.get(
                    `/maxscale/modules/${monitorModule}/fetch-cmd-results?${monitorId}`
                )
                // response ok
                if (status === 200) {
                    if (meta === 'switchover completed successfully.')
                        await dispatch('handleSwitchoverDone', { meta, successCb })
                    else
                        await dispatch('handleSwitchoverPending', {
                            meta,
                            monitorModule,
                            monitorId,
                            successCb,
                        })
                }
            } catch (e) {
                this.vue.$logger('store-monitor-checkSwitchOverRes').error(e)
            }
        },
        /**
         * @param {String} meta - meta string message
         * @param {Function} successCb - callback function after successfully switchover
         */
        async handleSwitchoverDone({ commit }, { meta, successCb }) {
            commit('SET_SNACK_BAR_MESSAGE', { text: [meta], type: 'success' }, { root: true })
            if (this.vue.$help.isFunction(successCb)) await successCb()
        },

        /**
         * This handles calling checkSwitchOverRes every 2500ms until receive success msg
         * @param {Object} meta - meta error object
         * @param {String} payload.monitorModule Monitor module
         * @param {String} payload.monitorId Monitor id
         * @param {Function} successCb - callback function after successfully switchover
         */
        async handleSwitchoverPending(
            { commit, dispatch },
            { meta, monitorModule, monitorId, successCb }
        ) {
            if (isSwitchingOver(meta.errors[0].detail)) {
                commit(
                    'SET_SNACK_BAR_MESSAGE',
                    { text: ['switchover is still running'], type: 'warning' },
                    { root: true }
                )
                // loop fetch until receive success meta
                await this.vue.$help.delay(2500).then(
                    async () =>
                        await dispatch('checkSwitchOverRes', {
                            monitorModule,
                            monitorId,
                            successCb,
                        })
                )
            } else {
                const errArr = meta.errors.map(error => error.detail)
                commit('SET_SNACK_BAR_MESSAGE', { text: errArr, type: 'error' }, { root: true })
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
        getMonitorOps: (state, getters, rootState) => {
            const { STOP, START, DESTROY, SWITCHOVER } = rootState.app_config.MONITOR_OP_TYPES
            // scope is needed to access $t
            return ({ currState, scope }) => ({
                [STOP]: {
                    text: scope.$t('monitorOps.actions.stop'),
                    type: STOP,
                    icon: ' $vuetify.icons.stopped',
                    iconSize: 22,
                    color: 'primary',
                    params: 'stop',
                    disabled: currState === 'Stopped',
                },
                [START]: {
                    text: scope.$t('monitorOps.actions.start'),
                    type: START,
                    icon: '$vuetify.icons.running',
                    iconSize: 22,
                    color: 'primary',
                    params: 'start',
                    disabled: currState === 'Running',
                },
                [DESTROY]: {
                    text: scope.$t('monitorOps.actions.destroy'),
                    type: DESTROY,
                    icon: '$vuetify.icons.delete',
                    iconSize: 18,
                    color: 'error',
                    disabled: false,
                },
                [SWITCHOVER]: {
                    text: scope.$t('monitorOps.actions.switchover'),
                    type: SWITCHOVER,
                    color: 'primary',
                    disabled: false,
                },
            })
        },
    },
}
