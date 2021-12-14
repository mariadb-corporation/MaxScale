/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import ax from 'axios'

const HEADERS = {
    'X-Requested-With': 'XMLHttpRequest',
    'Content-Type': 'application/json',
    'Cache-Control': 'no-cache',
}
const BASE_URL = '/'
const cancelToken = ax.CancelToken
const CANCEL_MESSAGE = 'Request canceled by user'
let cancelSource = cancelToken.source()
const refreshAxiosToken = () => {
    cancelSource = cancelToken.source()
}
const cancelAllRequests = () => {
    cancelSource.cancel(CANCEL_MESSAGE)
}
function baseConf() {
    return ax.create({
        baseURL: BASE_URL,
        headers: HEADERS,
    })
}
/**
 * Default handler for error response status codes
 */
async function defErrStatusHandler({ store, error }) {
    const { getErrorsArr, delay } = store.vue.$help
    store.commit('SET_SNACK_BAR_MESSAGE', {
        text: getErrorsArr(error),
        type: 'error',
    })
    /* When request is dispatched in a modal, an overlay_type loading will be set,
     * Turn it off before returning error
     */
    if (store.state.overlay_type !== null)
        await delay(600).then(() => store.commit('SET_OVERLAY_TYPE', null))
    return Promise.reject(error)
}

/**
 * @param {Object} payload.store - vuex store instance
 * @param {Object} payload.error - axios error object
 * @returns {Object} - returns an object with error response status codes as key and value as handler function
 */
function baseErrStatusHandlerMap({ store, error }) {
    return {
        401: async function() {
            // cancel all previous requests before logging out
            store.$cancelAllRequests()
            await store.dispatch('user/logout')
        },
        404: async function() {
            await store.router.push('/404')
        },
        null: function() {
            if (error.toString() === `Cancel: ${CANCEL_MESSAGE}`)
                // request is cancelled by user, so no response is received
                return Promise.reject(error)
            else
                return store.commit('SET_SNACK_BAR_MESSAGE', {
                    text: ['Lost connection to MaxScale, please check if MaxScale is running'],
                    type: 'error',
                })
        },
    }
}

// axios instance for auth endpoint
let authHttp = baseConf()
authHttp.interceptors.request.use(
    config => ({ ...config, cancelToken: cancelSource.token }),
    error => Promise.reject(error)
)

// axios instance for other endpoints except `/sql`
function http(store) {
    let http = baseConf()

    http.interceptors.request.use(
        config => ({ ...config, cancelToken: cancelSource.token }),
        error => Promise.reject(error)
    )
    http.interceptors.response.use(
        response => response,
        async error => {
            const { response: { status = null } = {} } = error || {}
            const errStatusHandlerMap = baseErrStatusHandlerMap({ store, error })
            if (Object.keys(errStatusHandlerMap).includes(`${status}`)) {
                await errStatusHandlerMap[status]()
            } else defErrStatusHandler({ store, error })
        }
    )
    return http
}

function updateIsQueryingMap({ store, value }) {
    const active_wke_id = store.state.query.active_wke_id
    if (active_wke_id)
        store.commit('query/UPDATE_IS_QUERYING_MAP', {
            id: active_wke_id,
            payload: value,
        })
}
/**
 * This function helps to check if there is a lost connection error that has either
 * 2006 or 2013 errno value and update the corresponding error message object to lost_cnn_err_msg_obj_map state
 * @param {Object} param.res - response of every request from queryHttp axios instance
 * @param {Object} param.store - vuex store
 */
function analyzeRes({ res, store }) {
    const results = store.vue.$typy(res, 'data.data.attributes.results').safeArray
    const lostCnnErrMsgs = results.filter(res => {
        const errno = store.vue.$typy(res, 'errno').safeNumber
        return store.state.app_config.MARIADB_NET_ERRNO.includes(errno)
    })
    if (lostCnnErrMsgs.length) {
        const active_wke_id = store.state.query.active_wke_id
        store.commit('query/UPDATE_LOST_CNN_ERR_MSG_OBJ_MAP', {
            id: active_wke_id,
            payload: lostCnnErrMsgs[0],
        })
    }
}
// axios instance for `/sql` endpoint
function queryHttp(store) {
    let queryHttp = baseConf()
    queryHttp.interceptors.request.use(
        config => {
            updateIsQueryingMap({ store, value: true })
            return { ...config, cancelToken: cancelSource.token }
        },
        error => Promise.reject(error)
    )
    queryHttp.interceptors.response.use(
        response => {
            updateIsQueryingMap({ store, value: false })
            analyzeRes({ res: response, store })
            return response
        },
        async error => {
            const { getErrorsArr } = store.vue.$help
            const { response: { status = null } = {} } = error || {}
            const errStatusHandlerMap = baseErrStatusHandlerMap({ store, error })
            if (Object.keys(errStatusHandlerMap).includes(`${status}`)) {
                await errStatusHandlerMap[status]()
            } else if (status === 503) {
                return store.commit('SET_SNACK_BAR_MESSAGE', {
                    text: [...getErrorsArr(error), 'Please reconnect'],
                    type: 'error',
                })
            } else defErrStatusHandler({ store, error })
            updateIsQueryingMap({ store, value: false })
        }
    )
    return queryHttp
}

export { refreshAxiosToken, cancelAllRequests, authHttp, http, queryHttp }
