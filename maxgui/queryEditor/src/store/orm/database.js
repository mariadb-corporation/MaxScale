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
import { Database } from '@vuex-orm/core'
import Editor from '@queryEditorSrc/store/orm/models/Editor'
import QueryConn from '@queryEditorSrc/store/orm/models/QueryConn'
import QueryResult from '@queryEditorSrc/store/orm/models/QueryResult'
import QueryTab from '@queryEditorSrc/store/orm/models/QueryTab'
import SchemaSidebar from '@queryEditorSrc/store/orm/models/SchemaSidebar'
import Worksheet from '@queryEditorSrc/store/orm/models/Worksheet'
// entities to be stored only in memory
import QueryTabMem from '@queryEditorSrc/store/orm/models/QueryTabMem'
import WorksheetMem from '@queryEditorSrc/store/orm/models/WorksheetMem'
// Store modules
import editors from '@queryEditorSrc/store/orm/modules/editors'
import queryConns from '@queryEditorSrc/store/orm/modules/queryConns'
import queryTabs from '@queryEditorSrc/store/orm/modules/queryTabs'
import worksheets from '@queryEditorSrc/store/orm/modules/worksheets'

const database = new Database()
//TODO: Register a model and vuex module to Database.
database.register(Editor, editors)
database.register(QueryConn, queryConns)
database.register(QueryResult)
database.register(QueryTab, queryTabs)
database.register(SchemaSidebar)
database.register(Worksheet, worksheets)
database.register(QueryTabMem)
database.register(WorksheetMem)

export default database
