/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-07-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { Database } from '@vuex-orm/core'
import Editor from '@wsModels/Editor'
import ErdTask from '@wsModels/ErdTask'
import EtlTask from '@wsModels/EtlTask'
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import QueryResult from '@wsModels/QueryResult'
import QueryTab from '@wsModels/QueryTab'
import SchemaSidebar from '@wsModels/SchemaSidebar'
import Worksheet from '@wsModels/Worksheet'
// entities to be stored only in memory
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import EtlTaskTmp from '@wsModels/EtlTaskTmp'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import QueryEditorTmp from '@wsModels/QueryEditorTmp'
import WorksheetTmp from '@wsModels/WorksheetTmp'
// Store modules
import editors from '@wsSrc/store/orm/modules/editors'
import erdTasks from '@wsSrc/store/orm/modules/erdTasks'
import etlTasks from '@wsSrc/store/orm/modules/etlTasks'
import queryConns from '@wsSrc/store/orm/modules/queryConns'
import queryEditors from '@wsSrc/store/orm/modules/queryEditors'
import queryResults from '@wsSrc/store/orm/modules/queryResults'
import queryTabs from '@wsSrc/store/orm/modules/queryTabs'
import schemaSidebars from '@wsSrc/store/orm/modules/schemaSidebars'
import worksheets from '@wsSrc/store/orm/modules/worksheets'

const database = new Database()
database.register(Editor, editors)
database.register(ErdTask, erdTasks)
database.register(EtlTask, etlTasks)
database.register(QueryConn, queryConns)
database.register(QueryEditor, queryEditors)
database.register(QueryResult, queryResults)
database.register(QueryTab, queryTabs)
database.register(SchemaSidebar, schemaSidebars)
database.register(Worksheet, worksheets)
database.register(ErdTaskTmp)
database.register(EtlTaskTmp)
database.register(QueryTabTmp)
database.register(QueryEditorTmp)
database.register(WorksheetTmp)

export default database
