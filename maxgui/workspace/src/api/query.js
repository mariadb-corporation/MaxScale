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
import http from '@workspaceSrc/utils/http'

/**
 * @param {String} id - connection ID
 * @param {Object} body - payload
 * @param {Object} [config] - axios config
 * @returns {Promise}
 */
export async function query({ id, body, config = {} }) {
    return await http().post(`/sql/${id}/queries`, body, config)
}
/**
 * @param {String} id - connection ID
 * @param {String} queryId - query ID
 * @returns {Promise}
 */
export async function getAsyncResult({ id, queryId }) {
    return await http().get(`/sql/${id}/queries/${queryId}`)
}
