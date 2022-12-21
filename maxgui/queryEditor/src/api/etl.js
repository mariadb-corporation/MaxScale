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
import http from '@queryEditorSrc/utils/http'

/**
 * Prepare ETL Operation
 * @param {String} id - ODBC source connection ID
 * @param {String} body.target - destination server connection id
 * @param {String} body.type - mariadb||postgresql||generic
 * @param {Array} body.tables - e.g. [{ "table": "t1", "schema": "test"}]
 * @param {Number} body.threads
 * @returns {Promise}
 */
export async function prepare({ id, body }) {
    return await http().post(`/sql/${id}/etl/prepare`, body)
}
/**
 * Start ETL operation
 * @param {Array} body.tables - Result from the prepare step.
 * @param {String} body.target - destination server connection id
 * @returns {Promise}
 */
export async function start({ id, body }) {
    return await http().post(`/sql/${id}/etl/start`, body)
}
/**
 * Cancel ETL operation
 * @param {String} id - ODBC source connection ID
 * @returns {Promise}
 */
export async function cancel(id) {
    return await http().post(`/sql/${id}/cancel`)
}
