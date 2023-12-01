/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import filter from './filter'
import listener from './listener'
import maxscale from './maxscale'
import monitor from './monitor'
import server from './server'
import service from './service'
import session from './session'
import user from './user'
import persisted from './persisted'
import visualization from './visualization'
import mxsApp from '@share/store/mxsApp'

export default {
    mxsApp,
    filter,
    listener,
    maxscale,
    monitor,
    server,
    service,
    session,
    user,
    persisted,
    visualization,
}
