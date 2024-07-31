/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { Database } from '@vuex-orm/core'
import AlterEditor from '@wsModels/AlterEditor'
import InsightViewer from '@wsModels/InsightViewer'
import ErdTask from '@wsModels/ErdTask'
import EtlTask from '@wsModels/EtlTask'
import DdlEditor from '@wsModels/DdlEditor'
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import QueryResult from '@wsModels/QueryResult'
import QueryTab from '@wsModels/QueryTab'
import SchemaSidebar from '@wsModels/SchemaSidebar'
import TxtEditor from '@wsModels/TxtEditor'
import Worksheet from '@wsModels/Worksheet'
// entities to be stored only in memory
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import EtlTaskTmp from '@wsModels/EtlTaskTmp'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import QueryEditorTmp from '@wsModels/QueryEditorTmp'
import WorksheetTmp from '@wsModels/WorksheetTmp'
// Store modules
import erdTasks from '@/store/orm/modules/erdTasks'
import etlTasks from '@/store/orm/modules/etlTasks'
import queryConns from '@/store/orm/modules/queryConns'
import queryEditors from '@/store/orm/modules/queryEditors'
import schemaSidebars from '@/store/orm/modules/schemaSidebars'
import worksheets from '@/store/orm/modules/worksheets'

const database = new Database()
database.register(AlterEditor)
database.register(InsightViewer)
database.register(ErdTask, erdTasks)
database.register(EtlTask, etlTasks)
database.register(DdlEditor)
database.register(QueryConn, queryConns)
database.register(QueryEditor, queryEditors)
database.register(QueryResult)
database.register(QueryTab)
database.register(SchemaSidebar, schemaSidebars)
database.register(Worksheet, worksheets)
database.register(TxtEditor)
database.register(ErdTaskTmp)
database.register(EtlTaskTmp)
database.register(QueryTabTmp)
database.register(QueryEditorTmp)
database.register(WorksheetTmp)

export default database
