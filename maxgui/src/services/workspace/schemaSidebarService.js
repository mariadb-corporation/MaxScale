/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import QueryEditorTmp from '@wsModels/QueryEditorTmp'
import SchemaSidebar from '@wsModels/SchemaSidebar'
import Worksheet from '@wsModels/Worksheet'
import queries from '@/api/sql/queries'
import queryConnService from '@wsServices/queryConnService'
import schemaNodeHelper from '@/utils/schemaNodeHelper'
import { tryAsync } from '@/utils/helpers'
import { t as typy } from 'typy'

async function fetchSchemas() {
  const config = Worksheet.getters('activeRequestConfig')
  const queryEditorId = QueryEditor.getters('activeId')
  const { id, meta: { name: connection_name } = {} } = QueryConn.getters('activeQueryTabConn')

  QueryEditorTmp.update({ where: queryEditorId, data: { loading_db_tree: true } })

  const [e, res] = await tryAsync(
    queries.post({ id, body: { sql: SchemaSidebar.getters('schemaSql') }, config })
  )
  if (e) QueryEditorTmp.update({ where: queryEditorId, data: { loading_db_tree: false } })
  else {
    const nodes = schemaNodeHelper.genNodes({
      queryResult: typy(res, 'data.data.attributes.results[0]').safeObject,
    })
    if (nodes.length)
      QueryEditorTmp.update({
        where: queryEditorId,
        data(obj) {
          obj.loading_db_tree = false
          obj.db_tree_of_conn = connection_name
          obj.db_tree = nodes
        },
      })
    else QueryEditorTmp.update({ where: queryEditorId, data: { loading_db_tree: false } })
  }
}

async function initFetch() {
  await fetchSchemas()
  await queryConnService.updateActiveDb()
}

export default { fetchSchemas, initFetch }
