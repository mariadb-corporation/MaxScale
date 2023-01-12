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
import editorsMem from './editorsMem'
import etlMem from './etlMem'
import fileSysAccess from './fileSysAccess'
import mxsApp from '@share/store/mxsApp'
import queryConnsMem from './queryConnsMem'
import mxsWorkspace from './mxsWorkspace'
import queryPersisted from './queryPersisted'

export default {
    editorsMem,
    etlMem,
    fileSysAccess,
    mxsApp,
    queryConnsMem,
    mxsWorkspace,
    queryPersisted,
}
