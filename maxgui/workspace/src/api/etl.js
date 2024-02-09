/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import base from '@wsSrc/api/base'

export default {
    /**
     * Prepare ETL Operation
     * @param {String} param.id - ODBC source connection ID
     * @param {String} param.body.target - destination server connection id
     * @param {String} param.body.type - mariadb||postgresql||generic
     * @param {Array}  param.body.tables - e.g. [{ "table": "t1", "schema": "test"}]
     * @param {Number} param.body.threads
     * @param {Object} param.config - axios config
     * @returns {Promise}
     */
    prepare: ({ id, body, config }) => base.post({ url: `/sql/${id}/etl/prepare`, body, config }),
    /**
     * Start ETL operation
     * @param {String} param.id - ODBC source connection ID
     * @param {Array} param.body.tables - Result from the prepare step.
     * @param {String} param.body.target - destination server connection id
     * @param {Object} param.config - axios config
     * @returns {Promise}
     */
    start: ({ id, body, config }) => base.post({ url: `/sql/${id}/etl/start`, body, config }),
}
