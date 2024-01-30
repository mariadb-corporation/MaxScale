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
import { MONITOR_OP_TYPES } from '@src/constants'
import { genSetMutations } from '@share/utils/helpers'

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

const states = () => ({
    all_monitors: [],
    current_monitor: {},
    monitor_diagnostics: {},
    curr_cs_status: {},
    is_loading_cs_status: false,
    cs_no_data_txt: '',
})

export default {
    namespaced: true,
    state: states(),
    mutations: genSetMutations(states()),
    actions: {
        async fetchAllMonitors({ commit }) {
            try {
                let res = await this.vue.$http.get(`/monitors`)
                if (res.data.data) commit('SET_ALL_MONITORS', res.data.data)
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },
        async fetchMonitorById({ commit }, id) {
            try {
                let res = await this.vue.$http.get(`/monitors/${id}`)
                if (res.data.data) commit('SET_CURRENT_MONITOR', res.data.data)
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },
        async fetchMonitorDiagnosticsById({ commit }, id) {
            try {
                let res = await this.vue.$http.get(
                    `/monitors/${id}?fields[monitors]=monitor_diagnostics`
                )
                if (res.data.data) commit('SET_MONITOR_DIAGNOSTICS', res.data.data)
            } catch (e) {
                this.vue.$logger.error(e)
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
                let res = await this.vue.$http.post(`/monitors/`, body)
                let message = [`Monitor ${payload.id} is created`]
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
                let res = await this.vue.$http.patch(`/monitors/${payload.id}`, body)
                // response ok
                if (res.status === 204) {
                    commit(
                        'mxsApp/SET_SNACK_BAR_MESSAGE',
                        {
                            text: [`Monitor ${payload.id} is updated`],
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
         * @param {String} param.id - id of the monitor to be manipulated
         * @param {String} param.type - type of operation: check MONITOR_OP_TYPES
         * @param {String|Object} param.opParams - operation params. For async call, it's an object
         * @param {Function} param.successCb - callback function after successfully updated
         * @param {Function} param.asyncCmdErrCb - callback function after fetch-cmd-result returns failed message
         * @param {Function} param.custAsyncCmdDone - callback function to replace handleAsyncCmdDone
         * @param {Boolean} param.showSnackbar - should show result message in the snackbar or not
         * @param {Number} param.pollingResInterval - interval time for polling fetch-cmd-result
         */
        async manipulateMonitor(
            { dispatch, commit },
            {
                id,
                type,
                opParams,
                successCb,
                asyncCmdErrCb,
                showSnackbar = true,
                custAsyncCmdDone,
                pollingResInterval = 2500,
            }
        ) {
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
                    CS_GET_STATUS,
                    CS_STOP_CLUSTER,
                    CS_START_CLUSTER,
                    CS_SET_READONLY,
                    CS_SET_READWRITE,
                    CS_ADD_NODE,
                    CS_REMOVE_NODE,
                } = MONITOR_OP_TYPES
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
                    case REJOIN:
                    case CS_GET_STATUS:
                    case CS_STOP_CLUSTER:
                    case CS_START_CLUSTER:
                    case CS_SET_READONLY:
                    case CS_SET_READWRITE:
                    case CS_ADD_NODE:
                    case CS_REMOVE_NODE: {
                        method = 'post'
                        const { moduleType, params } = opParams
                        url = `/maxscale/modules/${moduleType}/${type}?${id}${params}`
                        break
                    }
                }
                const res = await this.vue.$http[method](url)
                // response ok
                if (res.status === 204) {
                    switch (type) {
                        case SWITCHOVER:
                        case RESET_REP:
                        case RELEASE_LOCKS:
                        case FAILOVER:
                        case REJOIN:
                        case CS_GET_STATUS:
                        case CS_STOP_CLUSTER:
                        case CS_START_CLUSTER:
                        case CS_SET_READONLY:
                        case CS_SET_READWRITE:
                        case CS_ADD_NODE:
                        case CS_REMOVE_NODE:
                            await dispatch('checkAsyncCmdRes', {
                                cmdName: type,
                                monitorModule: opParams.moduleType,
                                monitorId: id,
                                successCb,
                                asyncCmdErrCb,
                                custAsyncCmdDone,
                                showSnackbar,
                                pollingResInterval,
                            })
                            break
                        default:
                            if (showSnackbar)
                                commit(
                                    'mxsApp/SET_SNACK_BAR_MESSAGE',
                                    { text: message, type: 'success' },
                                    { root: true }
                                )
                            await this.vue.$typy(successCb).safeFunction()
                            break
                    }
                }
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },
        /**
         * This function should be called right after an async cmd action is called
         * in order to show async cmd status message on snackbar.
         * @param {String} param.cmdName - async command name
         * @param {String} param.monitorModule Monitor module
         * @param {String} param.monitorId Monitor id
         * @param {Function} param.successCb - callback function after successfully performing an async cmd
         * @param {Function} param.asyncCmdErrCb - callback function after fetch-cmd-result returns failed message
         * @param {Function} param.custAsyncCmdDone - callback function to replace handleAsyncCmdDone
         * @param {Boolean} param.showSnackbar - should show result message in the snackbar or not
         * @param {Number} param.pollingResInterval - interval time for polling fetch-cmd-result
         */
        async checkAsyncCmdRes({ dispatch }, param) {
            try {
                const {
                    monitorModule,
                    monitorId,
                    successCb,
                    showSnackbar,
                    custAsyncCmdDone,
                } = param
                const { status, data: { meta } = {} } = await this.vue.$http.get(
                    `/maxscale/modules/${monitorModule}/fetch-cmd-result?${monitorId}`
                )
                // response ok
                if (status === 200) {
                    if (meta.errors) await dispatch('handleAsyncCmdPending', { ...param, meta })
                    else if (this.vue.$typy(custAsyncCmdDone).isFunction)
                        await custAsyncCmdDone(meta)
                    else await dispatch('handleAsyncCmdDone', { meta, successCb, showSnackbar })
                }
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },
        /**
         * @param {String} param.meta - meta string message
         * @param {Function} param.successCb - callback function after successfully performing an async cmd
         * @param {Boolean} param.showSnackbar - should show result message in the snackbar or not
         */
        async handleAsyncCmdDone({ commit }, { meta, successCb, showSnackbar }) {
            if (showSnackbar)
                commit(
                    'mxsApp/SET_SNACK_BAR_MESSAGE',
                    { text: [this.vue.$helpers.capitalizeFirstLetter(meta)], type: 'success' },
                    { root: true }
                )
            await this.vue.$typy(successCb).safeFunction(meta)
        },

        /**
         * This handles calling checkAsyncCmdRes every 2500ms until receive success msg
         * @param {Object} param.meta - meta error object
         * @param {String} param.cmdName - async command name
         * @param {String} param.monitorModule Monitor module
         * @param {String} param.monitorId Monitor id
         * @param {Function} param.successCb - callback function after successfully performing an async cmd
         * @param {Function} param.asyncCmdErrCb - callback function after fetch-cmd-result returns failed message
         * @param {Boolean} param.showSnackbar - should show result message in the snackbar or not
         * @param {Number} param.pollingResInterval - interval time for polling fetch-cmd-result
         */
        async handleAsyncCmdPending({ commit, dispatch }, param) {
            const { cmdName, meta, showSnackbar, asyncCmdErrCb, pollingResInterval } = param
            const { isRunning, isCancelled } = getAsyncCmdRunningStates({ meta, cmdName })
            if (isRunning && !isCancelled) {
                if (showSnackbar)
                    commit(
                        'mxsApp/SET_SNACK_BAR_MESSAGE',
                        // Remove `No manual commands are available`, shows only the latter part.
                        {
                            text: meta.errors.map(e =>
                                this.vue.$helpers.capitalizeFirstLetter(
                                    e.detail.replace(
                                        'No manual command results are available, ',
                                        ''
                                    )
                                )
                            ),
                            type: 'warning',
                        },
                        { root: true }
                    )
                // loop fetch until receive success meta
                await this.vue.$helpers
                    .delay(pollingResInterval)
                    .then(async () => await dispatch('checkAsyncCmdRes', param))
            } else {
                const errArr = meta.errors.map(error => error.detail)
                if (showSnackbar)
                    commit(
                        'mxsApp/SET_SNACK_BAR_MESSAGE',
                        { text: errArr, type: 'error' },
                        { root: true }
                    )
                await this.vue.$typy(asyncCmdErrCb).safeFunction(errArr)
            }
        },

        /**
         * This handles calling manipulateMonitor action
         * @param {String} param.monitorId Monitor id
         * @param {String} param.monitorModule - monitor module type
         * @param {Boolean} param.isCsCluster - Is a ColumnStore cluster or not
         * @param {String} param.monitorState - monitor state
         * @param {Function} param.successCb - callback function after successfully performing an async cmd
         * @param {Number} param.pollingResInterval - interval time for polling fetch-cmd-result
         */
        async handleFetchCsStatus(
            { state, commit, dispatch, rootGetters },
            { monitorId, monitorModule, isCsCluster, monitorState, successCb, pollingResInterval }
        ) {
            if (
                rootGetters['user/isAdmin'] &&
                isCsCluster &&
                !state.is_loading_cs_status &&
                monitorState !== 'Stopped'
            ) {
                const { CS_GET_STATUS } = MONITOR_OP_TYPES
                commit('SET_IS_LOADING_CS_STATUS', true)
                await dispatch('manipulateMonitor', {
                    id: monitorId,
                    type: CS_GET_STATUS,
                    showSnackbar: false,
                    successCb: async meta => {
                        commit('SET_CURR_CS_STATUS', meta)
                        commit('SET_CS_NO_DATA_TXT', '')
                        await this.vue.$typy(successCb).safeFunction(meta)
                    },
                    asyncCmdErrCb: meta => {
                        commit('SET_CURR_CS_STATUS', {})
                        commit('SET_CS_NO_DATA_TXT', meta.join(', '))
                    },
                    opParams: { moduleType: monitorModule, params: '' },
                    pollingResInterval,
                })
                commit('SET_IS_LOADING_CS_STATUS', false)
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

                res = await this.vue.$http.patch(`/monitors/${payload.id}/relationships/servers`, {
                    data: payload.servers,
                })
                message = [`Servers relationships of ${payload.id} is updated`]

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
    },
    getters: {
        // -------------- below getters are available only when fetchAllMonitors has been dispatched
        getTotalMonitors: state => state.all_monitors.length,
        getAllMonitorsMap: state => {
            let map = new Map()
            state.all_monitors.forEach(ele => {
                map.set(ele.id, ele)
            })
            return map
        },
        getMonitorOps: () => {
            const {
                STOP,
                START,
                DESTROY,
                SWITCHOVER,
                RESET_REP,
                RELEASE_LOCKS,
                FAILOVER,
                REJOIN,
                CS_GET_STATUS,
                CS_STOP_CLUSTER,
                CS_START_CLUSTER,
                CS_SET_READONLY,
                CS_SET_READWRITE,
                CS_ADD_NODE,
                CS_REMOVE_NODE,
            } = MONITOR_OP_TYPES
            // scope is needed to access $mxs_t
            return ({ currState, scope }) => ({
                [STOP]: {
                    text: scope.$mxs_t('monitorOps.actions.stop'),
                    type: STOP,
                    icon: '$vuetify.icons.mxs_stopped',
                    iconSize: 22,
                    color: 'primary',
                    params: 'stop',
                    disabled: currState === 'Stopped',
                },
                [START]: {
                    text: scope.$mxs_t('monitorOps.actions.start'),
                    type: START,
                    icon: '$vuetify.icons.mxs_running',
                    iconSize: 22,
                    color: 'primary',
                    params: 'start',
                    disabled: currState === 'Running',
                },
                [DESTROY]: {
                    text: scope.$mxs_t('monitorOps.actions.destroy'),
                    type: DESTROY,
                    icon: '$vuetify.icons.mxs_delete',
                    iconSize: 18,
                    color: 'error',
                    disabled: false,
                },
                [SWITCHOVER]: {
                    text: scope.$mxs_t(`monitorOps.actions.${SWITCHOVER}`),
                    type: SWITCHOVER,
                    icon: '$vuetify.icons.mxs_switchover',
                    iconSize: 24,
                    color: 'primary',
                    disabled: false,
                },
                [RESET_REP]: {
                    text: scope.$mxs_t(`monitorOps.actions.${RESET_REP}`),
                    type: RESET_REP,
                    icon: '$vuetify.icons.mxs_reload',
                    iconSize: 20,
                    color: 'primary',
                    disabled: false,
                },
                [RELEASE_LOCKS]: {
                    text: scope.$mxs_t(`monitorOps.actions.${RELEASE_LOCKS}`),
                    type: RELEASE_LOCKS,
                    icon: 'mdi-lock-open-outline',
                    iconSize: 24,
                    color: 'primary',
                    disabled: false,
                },
                [FAILOVER]: {
                    text: scope.$mxs_t(`monitorOps.actions.${FAILOVER}`),
                    type: FAILOVER,
                    icon: '$vuetify.icons.mxs_failover',
                    iconSize: 24,
                    disabled: false,
                },
                [REJOIN]: {
                    text: scope.$mxs_t(`monitorOps.actions.${REJOIN}`),
                    type: REJOIN,
                    //TODO: Add rejoin icon
                    disabled: false,
                },
                [CS_GET_STATUS]: { type: CS_GET_STATUS, disabled: false },
                [CS_STOP_CLUSTER]: {
                    text: scope.$mxs_t(`monitorOps.actions.${CS_STOP_CLUSTER}`),
                    type: CS_STOP_CLUSTER,
                    icon: '$vuetify.icons.mxs_stopped',
                    iconSize: 22,
                    color: 'primary',
                    disabled: false,
                },
                [CS_START_CLUSTER]: {
                    text: scope.$mxs_t(`monitorOps.actions.${CS_START_CLUSTER}`),
                    type: CS_START_CLUSTER,
                    icon: '$vuetify.icons.mxs_running',
                    iconSize: 22,
                    color: 'primary',
                    disabled: false,
                },
                [CS_SET_READONLY]: {
                    type: CS_SET_READONLY,
                    text: scope.$mxs_t(`monitorOps.actions.${CS_SET_READONLY}`),
                    icon: 'mdi-database-eye-outline',
                    iconSize: 24,
                    color: 'primary',
                    disabled: false,
                },
                [CS_SET_READWRITE]: {
                    type: CS_SET_READWRITE,
                    text: scope.$mxs_t(`monitorOps.actions.${CS_SET_READWRITE}`),
                    icon: 'mdi-database-edit-outline',
                    iconSize: 24,
                    color: 'primary',
                    disabled: false,
                },
                [CS_ADD_NODE]: {
                    type: CS_ADD_NODE,
                    text: scope.$mxs_t(`monitorOps.actions.${CS_ADD_NODE}`),
                    icon: 'mdi-plus',
                    iconSize: 24,
                    color: 'primary',
                    disabled: false,
                },
                [CS_REMOVE_NODE]: {
                    type: CS_REMOVE_NODE,
                    text: scope.$mxs_t(`monitorOps.actions.${CS_REMOVE_NODE}`),
                    icon: '$vuetify.icons.mxs_delete',
                    iconSize: 18,
                    color: 'error',
                    disabled: false,
                },
            })
        },
    },
}
