/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import base from '@wsSrc/api/base'
import { PERSIST_TOKEN_OPT } from '@share/constants'

export default {
    get: config => base.get({ url: '/sql', config }),
    getDrivers: config => base.get({ url: '/sql/odbc/drivers', config }),
    delete: ({ id, config }) => base.delete({ url: `/sql/${id}`, config }),
    /**
     * @param {String} param.body.target - either 'odbc' or name of a server||listener||service
     * @param {String} param.body[db] - default database
     * @param {Number} param.body[timeout] - timeout to create the connection
     * @param {String} param.body.user - required when target !== 'odbc'
     * @param {String} param.body.password - required when target !== 'odbc'
     * @param {String} param.body.connection_string - required when target === 'odbc'
     * @param {Object} param.config - axios config
     * @returns {Promise}
     */
    open: ({ body, config }) => base.post({ url: `/sql?${PERSIST_TOKEN_OPT}`, body, config }),
    /**
     * @param {String} param.id - connection to be cloned
     * @param {Object} param.config - axios config
     * @returns {Promise}
     */
    clone: ({ id, body, config }) =>
        base.post({ url: `/sql/${id}/clone?${PERSIST_TOKEN_OPT}`, body, config }),
    /**
     * @param {String} param.id - connection to be reconnected
     * @param {Object} param.config - axios config
     * @returns {Promise}
     */
    reconnect: ({ id, body, config }) => base.post({ url: `/sql/${id}/reconnect`, body, config }),
}
