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
import Editor from '@workspaceSrc/store/orm/models/Editor'
import EtlTask from '@workspaceSrc/store/orm/models/EtlTask'
import QueryConn from '@workspaceSrc/store/orm/models/QueryConn'
import QueryResult from '@workspaceSrc/store/orm/models/QueryResult'
import QueryTab from '@workspaceSrc/store/orm/models/QueryTab'
import SchemaSidebar from '@workspaceSrc/store/orm/models/SchemaSidebar'
import Worksheet from '@workspaceSrc/store/orm/models/Worksheet'
// entities to be stored only in memory
import QueryTabTmp from '@workspaceSrc/store/orm/models/QueryTabTmp'
import WorksheetTmp from '@workspaceSrc/store/orm/models/WorksheetTmp'
// Store modules
import editors from '@workspaceSrc/store/orm/modules/editors'
import etlTasks from '@workspaceSrc/store/orm/modules/etlTasks'
import queryConns from '@workspaceSrc/store/orm/modules/queryConns'
import queryResults from '@workspaceSrc/store/orm/modules/queryResults'
import queryTabs from '@workspaceSrc/store/orm/modules/queryTabs'
import schemaSidebars from '@workspaceSrc/store/orm/modules/schemaSidebars'
import worksheets from '@workspaceSrc/store/orm/modules/worksheets'

const database = new Database()
database.register(Editor, editors)
database.register(EtlTask, etlTasks)
database.register(QueryConn, queryConns)
database.register(QueryResult, queryResults)
database.register(QueryTab, queryTabs)
database.register(SchemaSidebar, schemaSidebars)
database.register(Worksheet, worksheets)
database.register(QueryTabTmp)
database.register(WorksheetTmp)

export default database
