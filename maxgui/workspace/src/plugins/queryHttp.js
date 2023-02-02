/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-12-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import ax from 'axios'
import { t } from 'typy'
import { lodash, getErrorsArr } from '@share/utils/helpers'
import { MARIADB_NET_ERRNO } from '@wsSrc/store/config'
import { handleNullStatusCode, defErrStatusHandler } from '@share/axios/handlers'
import QueryConn from '@wsModels/QueryConn'
/**
 *
 * @param {Boolean} param.value - is connection busy
 * @param {String} param.sql_conn_id - the connection id that the request is sent
 */
function updateConnBusyStatus({ value, sql_conn_id }) {
    QueryConn.update({
        where: sql_conn_id,
        data: { is_busy: value },
    })
}
/**
 * This function helps to check if there is a lost connection error that has either
 * 2006 or 2013 errno value and update the corresponding error message object to lost_cnn_err_msg_obj_map state
 * @param {Object} param.res - response of every request from queryHttp axios instance
 * @param {String} param.sql_conn_id - the connection id that the request is sent
 */
function analyzeRes({ res, sql_conn_id }) {
    const results = t(res, 'data.data.attributes.results').safeArray
    const lostCnnErrMsgs = results.filter(res => {
        const errno = t(res, 'errno').safeNumber
        return MARIADB_NET_ERRNO.includes(errno)
    })

    if (lostCnnErrMsgs.length) {
        QueryConn.update({
            where: sql_conn_id,
            data: { lost_cnn_err: lostCnnErrMsgs[0] },
        })
    }
}
function getSqlConnId(url) {
    const matched = /\/sql\/([a-zA-z0-9-]*?)\//g.exec(url) || []
    return matched.length > 1 ? matched[1] : null
}
/**
 * axios instance for workspace endpoint.
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
            config = lodash.merge(config, store.state.mxsWorkspace.axios_opts)
            updateConnBusyStatus({ value: true, sql_conn_id: getSqlConnId(config.url) })
            return { ...config }
        },
        error => Promise.reject(error)
    )
    queryHttp.interceptors.response.use(
        response => {
            updateConnBusyStatus({
                value: false,
                sql_conn_id: getSqlConnId(response.config.url),
            })
            analyzeRes({ res: response, sql_conn_id: getSqlConnId(response.config.url) })
            return response
        },
        async error => {
            const { response: { status = null, config: { url = '', method } = {} } = {} } =
                error || {}
            switch (status) {
                case null:
                    handleNullStatusCode({ store, error })
                    break
                case 404:
                case 503:
                    if (method !== 'delete')
                        store.commit(
                            'mxsApp/SET_SNACK_BAR_MESSAGE',
                            {
                                text: [
                                    ...getErrorsArr(error),
                                    'Connection expired, please reconnect.',
                                ],
                                type: 'error',
                            },
                            { root: true }
                        )
                    await QueryConn.dispatch('validateConns', { silentValidation: true })
                    break
                default:
                    defErrStatusHandler({ store, error })
            }
            updateConnBusyStatus({ value: false, sql_conn_id: getSqlConnId(url) })
            return Promise.reject(error)
        }
    )
    return queryHttp
}

export default {
    install: (Vue, { store }) => {
        Vue.prototype.$queryHttp = queryHttp(store)
    },
}
