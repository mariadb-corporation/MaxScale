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
import Worksheet from './models/Worksheet'
import SchemaSidebar from './models/SchemaSidebar'
import QueryTab from './models/QueryTab'
import QueryResult from './models/QueryResult'
import QueryConn from './models/QueryConn'
import Editor from './models/Editor'

const database = new Database()
//TODO: Register a model and vuex module to Database.
database.register(Worksheet)
database.register(SchemaSidebar)
database.register(QueryTab)
database.register(QueryResult)
database.register(QueryConn)
database.register(Editor)

export default database
