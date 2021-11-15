/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
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

let authHttp = ax.create({
    baseURL: BASE_URL,
    headers: HEADERS,
})
authHttp.interceptors.request.use(
    function(config) {
        config.cancelToken = cancelSource.token
        return config
    },
    function(error) {
        return Promise.reject(error)
    }
)

function http(store) {
    let http = ax.create({
        baseURL: BASE_URL,
        headers: HEADERS,
    })

    http.interceptors.request.use(
        function(config) {
            config.cancelToken = cancelSource.token
            return config
        },
        function(error) {
            return Promise.reject(error)
        }
    )
    http.interceptors.response.use(
        response => {
            return response
        },
        async error => {
            const { getErrorsArr, delay } = store.vue.$help
            const { response: { status = null } = {} } = error || {}
            switch (status) {
                case 401:
                    // cancel all previous requests before logging out
                    store.$cancelAllRequests()
                    await store.dispatch('user/logout')
                    break
                case 404:
                    await store.router.push('/404')
                    break
                case null:
                    if (error.toString() === `Cancel: ${CANCEL_MESSAGE}`)
                        // request is cancelled by user, so no response is received
                        return Promise.reject(error)
                    else
                        return store.commit('SET_SNACK_BAR_MESSAGE', {
                            text: [
                                'Lost connection to MaxScale, please check if MaxScale is running',
                            ],
                            type: 'error',
                        })
                case 503: {
                    return store.commit('SET_SNACK_BAR_MESSAGE', {
                        text: [...getErrorsArr(error), 'Please reconnect'],
                        type: 'error',
                    })
                }
                default:
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
        }
    )
    return http
}
export { refreshAxiosToken, cancelAllRequests, authHttp, http }
