/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-08-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import ax from 'axios'
import { handleNullStatusCode, defErrStatusHandler } from '@share/axios/handlers'

/**
 *
 * @param {Object} param.store - vuex store
 * @param {Boolean} param.value - is connection busy
 * @param {String} param.sql_conn_id - the connection id that the request is sent
 */
function patchIsConnBusyMap({ store, value, sql_conn_id }) {
    const { id: active_session_id } =
        store.getters['querySession/getSessionByConnId'](sql_conn_id) || {}
    if (active_session_id)
        store.commit('queryConn/PATCH_IS_CONN_BUSY_MAP', {
            id: active_session_id,
            payload: { value },
        })
}
/**
 * This function helps to check if there is a lost connection error that has either
 * 2006 or 2013 errno value and update the corresponding error message object to lost_cnn_err_msg_obj_map state
 * @param {Object} param.res - response of every request from queryHttp axios instance
 * @param {Object} param.store - vuex store
 * @param {String} param.sql_conn_id - the connection id that the request is sent
 */
function analyzeRes({ res, store, sql_conn_id }) {
    const results = store.vue.$typy(res, 'data.data.attributes.results').safeArray
    const lostCnnErrMsgs = results.filter(res => {
        const errno = store.vue.$typy(res, 'errno').safeNumber
        return store.state.queryEditorConfig.config.MARIADB_NET_ERRNO.includes(errno)
    })
    if (lostCnnErrMsgs.length) {
        const { id: active_session_id } =
            store.getters['querySession/getSessionByConnId'](sql_conn_id) || {}
        store.commit('queryConn/PATCH_LOST_CNN_ERR_MSG_OBJ_MAP', {
            id: active_session_id,
            payload: { value: lostCnnErrMsgs[0] },
        })
    }
}
function getSqlConnId(url) {
    const matched = /\/sql\/([a-zA-z0-9-]*?)\//g.exec(url) || []
    return matched.length > 1 ? matched[1] : null
}
/**
 * axios instance for query editor endpoint.
 * Use this for sql connection endpoint so that the value for
 * is_conn_busy_map can be set accurately.
 * @param {Object} store -vuex store
 * @returns {Object} axios instance
 */
function queryHttp(store) {
    let queryHttp = ax.create({
        baseURL: '/',
        headers: {
            'X-Requested-With': 'XMLHttpRequest',
            'Content-Type': 'application/json',
            'Cache-Control': 'no-cache',
        },
    })
    queryHttp.interceptors.request.use(
        config => {
            config = store.vue.$helpers.lodash.merge(
                config,
                store.state.queryEditorConfig.axios_opts
            )
            patchIsConnBusyMap({ store, value: true, sql_conn_id: getSqlConnId(config.url) })
            return { ...config }
        },
        error => Promise.reject(error)
    )
    queryHttp.interceptors.response.use(
        response => {
            patchIsConnBusyMap({
                store,
                value: false,
                sql_conn_id: getSqlConnId(response.config.url),
            })
            analyzeRes({ res: response, store, sql_conn_id: getSqlConnId(response.config.url) })
            return response
        },
        async error => {
            const { getErrorsArr } = store.vue.$helpers
            const { response: { status = null, config: { url = '' } = {} } = {} } = error || {}
            switch (status) {
                case null:
                    handleNullStatusCode({ store, error })
                    break
                case 404:
                case 503:
                    store.commit(
                        'mxsApp/SET_SNACK_BAR_MESSAGE',
                        {
                            text: [...getErrorsArr(error), 'Please reconnect'],
                            type: 'error',
                        },
                        { root: true }
                    )
                    break
                default:
                    defErrStatusHandler({ store, error })
            }
            patchIsConnBusyMap({ store, value: false, sql_conn_id: getSqlConnId(url) })
        }
    )
    return queryHttp
}

export default {
    install: (Vue, { store }) => {
        Vue.prototype.$queryHttp = queryHttp(store)
    },
}
