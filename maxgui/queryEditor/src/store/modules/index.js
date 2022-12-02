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
/**
 * TODO: keep only queryPersisted, queryEditorConfig and mem modules
 * other ORM related modules should be registered via vuex-orm
 */
import queryConns from './queryConns'
import editors from './editors'
import editorsMem from './editorsMem'
import schemaSidebar from './schemaSidebar'
import queryResult from './queryResult'
import queryPersisted from './queryPersisted'
import queryEditorConfig from './queryEditorConfig'
import mxsApp from '@share/store/mxsApp'

export default {
    mxsApp,
    queryConns,
    editors,
    editorsMem,
    schemaSidebar,
    queryResult,
    queryPersisted,
    queryEditorConfig,
}
