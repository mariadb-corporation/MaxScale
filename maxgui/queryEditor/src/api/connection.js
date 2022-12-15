/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Vue from 'vue'
import commonConfig from '@share/config'

const { PERSIST_TOKEN_OPT } = commonConfig
/**
 * $queryHttp is available when query-editor is registered as a plugin
 * @returns axios instance
 */
const http = () => Vue.prototype.$queryHttp

/**
 * @returns {Promise}
 */
export async function getAliveConns() {
    return await http().get(`/sql`)
}

/**
 * @param {String} body.target - either 'odbc' or name of a server||listener||service
 * @param {String} [body.db] - default database
 * @param {Number} [body.timeout] - timeout to create the connection
 * @param {String} body.user - required when target !== 'odbc'
 * @param {String} body.password - required when target !== 'odbc'
 * @param {String} body.connection_string - required when target === 'odbc'
 * @returns {Promise}
 */
export async function openConn(body) {
    return await http().post(`/sql?${PERSIST_TOKEN_OPT}`, body)
}

/**
 * @param {String} connId - connection to be cloned
 * @returns {Promise}
 */
export async function cloneConn(connId) {
    return await http().post(`/sql/${connId}/clone?${PERSIST_TOKEN_OPT}`)
}

/**
 * @param {String} connId - connection to be reconnected
 * @returns {Promise}
 */
export async function reconnect(connId) {
    return await http().post(`/sql/${connId}/reconnect`)
}

/**
 * @param {String} connId - connection to be reconnected
 * @returns {Promise}
 */
export async function deleteConn(connId) {
    return await http().delete(`/sql/${connId}`)
}

/**
 * @returns {Promise}
 */
export async function getDrivers() {
    return await http().get('/sql/odbc/drivers')
}
