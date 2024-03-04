/*
 * Copyright (c) 2023 MariaDB plc
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
import { MONITOR_OP_TYPES } from '@/constants'

/**
 * @param {object} currState - computed property
 * @returns {object}
 */
export function useMonitorOpMap(currState) {
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
  const { t } = useI18n()
  const goBack = useGoBack()
  const opCall = useMonitorOpCall()
  return {
    map: computed(() => ({
      [STOP]: {
        title: t('monitorOps.actions.stop'),
        type: STOP,
        icon: 'mxs:stopped',
        iconSize: 22,
        color: 'primary',
        params: 'stop',
        disabled: currState.value === 'Stopped',
      },
      [START]: {
        title: t('monitorOps.actions.start'),
        type: START,
        icon: 'mxs:running',
        iconSize: 22,
        color: 'primary',
        params: 'start',
        disabled: currState.value === 'Running',
      },
      [DESTROY]: {
        title: t('monitorOps.actions.destroy'),
        type: DESTROY,
        icon: 'mxs:delete',
        iconSize: 18,
        color: 'error',
        disabled: false,
      },
      [SWITCHOVER]: {
        title: t(`monitorOps.actions.${SWITCHOVER}`),
        type: SWITCHOVER,
        icon: 'mxs:switchover',
        iconSize: 24,
        color: 'primary',
        disabled: false,
        saveText: 'swap',
      },
      [RESET_REP]: {
        title: t(`monitorOps.actions.${RESET_REP}`),
        type: RESET_REP,
        icon: 'mxs:reload',
        iconSize: 20,
        color: 'primary',
        disabled: false,
        saveText: 'reset',
      },
      [RELEASE_LOCKS]: {
        title: t(`monitorOps.actions.${RELEASE_LOCKS}`),
        type: RELEASE_LOCKS,
        icon: '$mdiLockOpenOutline',
        iconSize: 24,
        color: 'primary',
        disabled: false,
        saveText: 'release',
      },
      [FAILOVER]: {
        title: t(`monitorOps.actions.${FAILOVER}`),
        type: FAILOVER,
        icon: 'mxs:failover',
        iconSize: 24,
        disabled: false,
        saveText: 'perform',
      },
      [REJOIN]: {
        title: t(`monitorOps.actions.${REJOIN}`),
        type: REJOIN,
        //TODO: Add rejoin icon
        disabled: false,
      },
      [CS_GET_STATUS]: { type: CS_GET_STATUS, disabled: false },
      [CS_STOP_CLUSTER]: {
        title: t(`monitorOps.actions.${CS_STOP_CLUSTER}`),
        type: CS_STOP_CLUSTER,
        icon: 'mxs:stopped',
        iconSize: 22,
        color: 'primary',
        disabled: false,
        saveText: 'stop',
      },
      [CS_START_CLUSTER]: {
        title: t(`monitorOps.actions.${CS_START_CLUSTER}`),
        type: CS_START_CLUSTER,
        icon: 'mxs:running',
        iconSize: 22,
        color: 'primary',
        disabled: false,
        saveText: 'start',
      },
      [CS_SET_READONLY]: {
        type: CS_SET_READONLY,
        title: t(`monitorOps.actions.${CS_SET_READONLY}`),
        icon: '$mdiDatabaseEyeOutline',
        iconSize: 24,
        color: 'primary',
        disabled: false,
        saveText: 'set',
      },
      [CS_SET_READWRITE]: {
        type: CS_SET_READWRITE,
        title: t(`monitorOps.actions.${CS_SET_READWRITE}`),
        icon: '$mdiDatabaseEditOutline',
        iconSize: 24,
        color: 'primary',
        disabled: false,
        saveText: 'set',
      },
      [CS_ADD_NODE]: {
        type: CS_ADD_NODE,
        title: t(`monitorOps.actions.${CS_ADD_NODE}`),
        icon: '$mdiPlus',
        iconSize: 24,
        color: 'primary',
        disabled: false,
        saveText: 'add',
      },
      [CS_REMOVE_NODE]: {
        type: CS_REMOVE_NODE,
        title: t(`monitorOps.actions.${CS_REMOVE_NODE}`),
        icon: 'mxs:delete',
        iconSize: 18,
        color: 'error',
        disabled: false,
        saveText: 'remove',
      },
    })),
    handler: async ({ op, id, module, successCb, opParams, csPayload = {} }) => {
      const type = op.type
      let payload = { id, type, successCb }
      switch (type) {
        case SWITCHOVER:
        case RESET_REP:
        case RELEASE_LOCKS:
        case FAILOVER:
          payload = { ...payload, opParams: opParams || { module, params: '' } }
          break
        case STOP:
        case START:
          payload = { ...payload, opParams: op.params }
          break
        case DESTROY:
          payload = { ...payload, successCb: goBack }
          break
        case CS_STOP_CLUSTER:
        case CS_START_CLUSTER:
        case CS_SET_READWRITE:
        case CS_SET_READONLY:
        case CS_ADD_NODE:
        case CS_REMOVE_NODE:
          payload = { ...payload, ...csPayload }
          break
      }
      await opCall(payload)
    },
  }
}

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
    const isNotDone = states.some((s) => e.detail.includes(s))
    const hasCancelledTxt = e.detail.includes('cancelled')
    if (isNotDone) isRunning = isNotDone
    if (hasCancelledTxt) isCancelled = hasCancelledTxt
  }
  return { isRunning, isCancelled }
}

export function useFetchCmdRes() {
  const { tryAsync } = useHelpers()
  const http = useHttp()
  const typy = useTypy()
  const successHandler = useOperationSuccessHandler()
  const pollingCmdRes = usePollingCmdRes(fetch)
  /**
   * This function should be called right after an async cmd action is called
   * in order to show async cmd status message on snackbar.
   * @param {String} param.id Monitor id
   * @param {String} param.cmdName - async command name
   * @param {String} param.module module type
   * @param {Function} param.successCb - callback function after successfully performing an async cmd
   * @param {Function} param.asyncCmdErrCb - callback function after fetch-cmd-result returns failed message
   * @param {Function} param.asyncCmdSuccessHandler - callback function to replace useOperationSuccessHandler
   * @param {Boolean} param.showSnackbar - should show result message in the snackbar or not
   * @param {Number} param.pollingInterval - interval time for polling fetch-cmd-result
   */
  async function fetch(param) {
    const { module, id, successCb, showSnackbar, asyncCmdSuccessHandler } = param
    const [, { status, data: { meta } = {} }] = await tryAsync(
      http.get(`/maxscale/modules/${module}/fetch-cmd-result?${id}`)
    )
    // response ok
    if (status === 200)
      if (meta.errors) await pollingCmdRes({ ...param, meta })
      else if (typy(asyncCmdSuccessHandler).isFunction) await asyncCmdSuccessHandler(meta)
      else await successHandler({ meta, successCb, showSnackbar })
  }
  return fetch
}

export function usePollingCmdRes(fetch) {
  const store = useStore()
  const typy = useTypy()
  const fetchCmdRes = fetch
  const { delay } = useHelpers()
  /**
   * This handles calling checkAsyncCmdRes every 2500ms until receive success msg
   * @param {Object} param.meta - meta error object
   * @param {String} param.cmdName - async command name
   * @param {String} param.module module type
   * @param {String} param.monitorId Monitor id
   * @param {Function} param.successCb - callback function after successfully performing an async cmd
   * @param {Function} param.asyncCmdErrCb - callback function after fetch-cmd-result returns failed message
   * @param {Boolean} param.showSnackbar - should show result message in the snackbar or not
   * @param {Number} param.pollingInterval - interval time for polling fetch-cmd-result
   */
  return async (param) => {
    const { cmdName, meta, showSnackbar, asyncCmdErrCb, pollingInterval } = param
    const { isRunning, isCancelled } = getAsyncCmdRunningStates({ meta, cmdName })
    if (isRunning && !isCancelled) {
      if (showSnackbar)
        store.commit(
          'mxsApp/SET_SNACK_BAR_MESSAGE',
          // Remove `No manual commands are available`, shows only the latter part.
          {
            text: meta.errors.map((e) =>
              e.detail.replace('No manual command results are available, ', '')
            ),
            type: 'warning',
          }
        )
      // loop fetch until receive success meta
      await delay(pollingInterval).then(async () => await fetchCmdRes(param))
    } else {
      const errArr = meta.errors.map((error) => error.detail)
      if (showSnackbar)
        store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', { text: errArr, type: 'error' })
      await typy(asyncCmdErrCb).safeFunction(errArr)
    }
  }
}

export function useOperationSuccessHandler() {
  const store = useStore()
  const typy = useTypy()
  /**
   * @param {String} param.meta - meta string message
   * @param {Function} param.successCb - callback function after successfully performing an async cmd
   * @param {Boolean} param.showSnackbar - should show result message in the snackbar or not
   */
  return async ({ meta, successCb, showSnackbar }) => {
    if (showSnackbar)
      store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
        text: [meta],
        type: 'success',
      })
    await typy(successCb).safeFunction(meta)
  }
}

export function useMonitorOpCall() {
  const { tryAsync } = useHelpers()
  const http = useHttp()
  const fetchCmdRes = useFetchCmdRes()
  const store = useStore()
  const typy = useTypy()
  /**
   * @param {String} param.id - id of the monitor to be manipulated
   * @param {String} param.type - type of operation: check MONITOR_OP_TYPES
   * @param {String|Object} param.opParams - operation params. For async call, it's an object
   * @param {Function} param.successCb - callback function after successfully updated
   * @param {Function} param.asyncCmdErrCb - callback function after fetch-cmd-result returns failed message
   * @param {Function} param.asyncCmdSuccessHandler - function to use instead of useOperationSuccessHandler
   * @param {Boolean} param.showSnackbar - should show result message in the snackbar or not
   * @param {Number} param.pollingInterval - interval time for polling fetch-cmd-result
   */
  return async ({
    id,
    type,
    opParams,
    successCb,
    asyncCmdErrCb,
    asyncCmdSuccessHandler,
    showSnackbar = true,
    pollingInterval = 2500,
  }) => {
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
        const { module, params } = opParams
        url = `/maxscale/modules/${module}/${type}?${id}${params}`
        break
      }
    }
    const [, res] = await tryAsync(http[method](url))
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
          await fetchCmdRes({
            id,
            cmdName: type,
            module: opParams.module,
            successCb,
            asyncCmdErrCb,
            asyncCmdSuccessHandler,
            showSnackbar,
            pollingInterval,
          })
          break
        default:
          if (showSnackbar)
            store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', { text: message, type: 'success' })
          await typy(successCb).safeFunction()
          break
      }
    }
  }
}

export function useFetchCsStatus() {
  const typy = useTypy()
  let isLoading = ref(false)
  let csStatus = ref({})
  let noDataTxt = ref('')
  const operationCall = useMonitorOpCall()
  return {
    isLoading,
    csStatus,
    noDataTxt,
    /**
     * This handles calling manipulateMonitor action
     * @param {String} param.id Monitor id
     * @param {String} param.module - monitor module type
     * @param {String} param.state - monitor state
     * @param {Function} param.successCb - callback function after successfully performing an async cmd
     * @param {Number} param.pollingInterval - interval time for polling fetch-cmd-result
     */
    fetch: async ({ id, module, state, successCb, pollingInterval }) => {
      if (!isLoading.value && state !== 'Stopped') {
        const { CS_GET_STATUS } = MONITOR_OP_TYPES
        isLoading.value = true
        await operationCall({
          id,
          type: CS_GET_STATUS,
          opParams: { module, params: '' },
          successCb: async (meta) => {
            csStatus.value = meta
            noDataTxt.value = ''
            await typy(successCb).safeFunction(meta)
          },
          asyncCmdErrCb: (meta) => {
            csStatus.value = {}
            noDataTxt.value = meta.join(', ')
          },
          showSnackbar: false,
          pollingInterval,
        })
        isLoading.value = false
      }
    },
  }
}
