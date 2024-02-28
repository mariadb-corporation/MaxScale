/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import base from '@wsSrc/api/base'

export default {
    /**
     * @param {String} param.id - connection ID
     * @param {String} param.queryId - query ID
     * @param {Object} param.config - axios config
     * @returns {Promise}
     */
    getAsyncRes: ({ id, queryId, config }) =>
        base.get({ url: `/sql/${id}/queries/${queryId}`, config }),
    /**
     * @param {String} param.id - connection ID
     * @param {Object} param.body - request body
     * @param {Object} param.config - axios config
     * @returns {Promise}
     */
    post: ({ id, body, config }) => base.post({ url: `/sql/${id}/queries`, body, config }),
    /**
     * Cancel active queries on ODBC connection
     * @param {String} param.id - ODBC connection ID
     * @param {Object} param.config - axios config
     * @returns {Promise}
     */
    cancel: ({ id, body, config }) => base.post({ url: `/sql/${id}/cancel`, body, config }),
}
