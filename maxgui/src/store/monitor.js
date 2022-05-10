/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-05-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/**
 * @param {Object} param.meta -
 * @returns {Object} - {isRunning, isCancelled}
 */
function getAsyncCmdRunningStates({ meta, cmdName }) {
    let isRunning = false,
        isCancelled = false
    const cmd = cmdName.replace('async-', '')
    const states = [`${cmd} is still running`, `${cmd} is still pending`]
    for (const e of meta.errors) {
        const isNotDone = states.some(s => e.detail.includes(s))
        const hasCancelledTxt = e.detail.includes('cancelled')
        if (isNotDone) isRunning = isNotDone
        if (hasCancelledTxt) isCancelled = hasCancelledTxt
    }
    return { isRunning, isCancelled }
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
         * @param {String} param.type - type of operation: check MONITOR_OP_TYPES
         * @param {String|Object} param.opParams - operation params. For async call, it's an object
         * @param {Function} param.callback callback function after successfully updated
         */
        async manipulateMonitor({ dispatch, commit, rootState }, { id, type, opParams, callback }) {
            try {
                let url = `/monitors/${id}/${opParams}`,
                    method = 'put',
                    message
                const {
                    STOP,
                    START,
                    DESTROY,
                    SWITCHOVER,
                    RESET_REP,
                    RELEASE_LOCKS,
                    FAILOVER,
                    REJOIN,
                } = rootState.app_config.MONITOR_OP_TYPES
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
                    case SWITCHOVER:
                    case RESET_REP:
                    case RELEASE_LOCKS:
                    case FAILOVER:
                    case REJOIN: {
                        method = 'post'
                        const { moduleType, params } = opParams
                        url = `/maxscale/modules/${moduleType}/${type}?${id}${params}`
                        break
                    }
                }
                const res = await this.$http[method](url)
                // response ok
                if (res.status === 204) {
                    switch (type) {
                        case SWITCHOVER:
                        case RESET_REP:
                        case RELEASE_LOCKS:
                        case FAILOVER:
                        case REJOIN:
                            await dispatch('checkAsyncCmdRes', {
                                cmdName: type,
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
         * This function should be called right after an async cmd action is called
         * in order to show async cmd status message on snackbar.
         * @param {String} payload.cmdName - async command name
         * @param {String} payload.monitorModule Monitor module
         * @param {String} payload.monitorId Monitor id
         * @param {Function} successCb - callback function after successfully performing an async cmd
         */
        async checkAsyncCmdRes({ dispatch }, { cmdName, monitorModule, monitorId, successCb }) {
            try {
                const { status, data: { meta } = {} } = await this.$http.get(
                    `/maxscale/modules/${monitorModule}/fetch-cmd-results?${monitorId}`
                )
                // response ok
                if (status === 200) {
                    if (`${meta}`.includes('completed successfully'))
                        await dispatch('handleAsyncCmdDone', { meta, successCb })
                    else
                        await dispatch('handleAsyncCmdPending', {
                            cmdName,
                            meta,
                            monitorModule,
                            monitorId,
                            successCb,
                        })
                }
            } catch (e) {
                this.vue.$logger('store-monitor-checkAsyncCmdRes').error(e)
            }
        },
        /**
         * @param {String} meta - meta string message
         * @param {Function} successCb - callback function after successfully switchover
         */
        async handleAsyncCmdDone({ commit }, { meta, successCb }) {
            commit(
                'SET_SNACK_BAR_MESSAGE',
                { text: [this.vue.$help.capitalizeFirstLetter(meta)], type: 'success' },
                { root: true }
            )
            if (this.vue.$help.isFunction(successCb)) await successCb()
        },

        /**
         * This handles calling checkAsyncCmdRes every 2500ms until receive success msg
         * @param {String} payload.cmdName - async command name
         * @param {Object} payload.meta - meta error object
         * @param {String} payload.monitorModule Monitor module
         * @param {String} payload.monitorId Monitor id
         * @param {Function} payload.successCb - callback function after successfully performing an async cmd
         */
        async handleAsyncCmdPending(
            { commit, dispatch },
            { cmdName, meta, monitorModule, monitorId, successCb }
        ) {
            const { isRunning, isCancelled } = getAsyncCmdRunningStates({ meta, cmdName })
            if (isRunning && !isCancelled) {
                commit(
                    'SET_SNACK_BAR_MESSAGE',
                    // Remove `No manual commands are available`, shows only the latter part.
                    {
                        text: meta.errors.map(e =>
                            this.vue.$help.capitalizeFirstLetter(
                                e.detail.replace('No manual command results are available, ', '')
                            )
                        ),
                        type: 'warning',
                    },
                    { root: true }
                )
                // loop fetch until receive success meta
                await this.vue.$help.delay(2500).then(
                    async () =>
                        await dispatch('checkAsyncCmdRes', {
                            cmdName,
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
            const {
                STOP,
                START,
                DESTROY,
                SWITCHOVER,
                RESET_REP,
                RELEASE_LOCKS,
                FAILOVER,
                REJOIN,
            } = rootState.app_config.MONITOR_OP_TYPES
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
                    text: scope.$t(`monitorOps.actions.${SWITCHOVER}`),
                    type: SWITCHOVER,
                    icon: '$vuetify.icons.switchover',
                    iconSize: 24,
                    color: 'primary',
                    disabled: false,
                },
                [RESET_REP]: {
                    text: scope.$t(`monitorOps.actions.${RESET_REP}`),
                    type: RESET_REP,
                    icon: '$vuetify.icons.reload',
                    iconSize: 20,
                    color: 'primary',
                    disabled: false,
                },
                [RELEASE_LOCKS]: {
                    text: scope.$t(`monitorOps.actions.${RELEASE_LOCKS}`),
                    type: RELEASE_LOCKS,
                    icon: 'mdi-lock-open-outline',
                    iconSize: 24,
                    color: 'primary',
                    disabled: false,
                },
                [FAILOVER]: {
                    text: scope.$t(`monitorOps.actions.${FAILOVER}`),
                    type: FAILOVER,
                    icon: '$vuetify.icons.failover',
                    iconSize: 24,
                    disabled: false,
                },
                [REJOIN]: {
                    text: scope.$t(`monitorOps.actions.${REJOIN}`),
                    type: REJOIN,
                    //TODO: Add rejoin icon
                    disabled: false,
                },
            })
        },
    },
}
