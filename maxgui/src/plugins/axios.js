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
import Vue from 'vue'
import ax from 'axios'
import store from 'store'

const cancelToken = ax.CancelToken
const CANCEL_MESSAGE = 'Request canceled by user'
export let cancelSource = cancelToken.source()
export const refreshAxiosToken = () => {
    cancelSource = cancelToken.source()
}
export const cancelAllRequests = () => {
    cancelSource.cancel(CANCEL_MESSAGE)
}

let apiClient = ax.create({
    baseURL: '/',
    headers: {
        'X-Requested-With': 'XMLHttpRequest',
        'Content-Type': 'application/json',
        'Cache-Control': 'no-cache',
    },
})

apiClient.interceptors.request.use(
    function(config) {
        config.cancelToken = cancelSource.token
        return config
    },
    function(error) {
        return Promise.reject(error)
    }
)

apiClient.interceptors.response.use(
    response => {
        return response
    },
    async error => {
        const { getErrorsArr, delay } = store.vue.$help
        const { response: { status = null } = {} } = error || {}
        switch (status) {
            case 401:
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
                        text: ['Lost connection to MaxScale, please check if MaxScale is running'],
                        type: 'error',
                    })
            default:
                store.commit('SET_SNACK_BAR_MESSAGE', {
                    text: getErrorsArr(error),
                    type: 'error',
                })
                /*
                    When request is dispatched in a modal, an overlay_type loading will be set,
                    Turn it off before returning error
                */
                if (store.state.overlay_type !== null)
                    await delay(600).then(() => store.commit('SET_OVERLAY_TYPE', null))

                return Promise.reject(error)
        }
    }
)

const loginAxios = ax.create({
    baseURL: '/',
    headers: {
        'X-Requested-With': 'XMLHttpRequest',
        'Content-Type': 'application/json',
        'Cache-Control': 'no-cache',
    },
})

// immutable axios instances
Object.defineProperties(Vue.prototype, {
    $axios: {
        get() {
            return apiClient
        },
    },

    $loginAxios: {
        get() {
            return loginAxios
        },
    },
})
