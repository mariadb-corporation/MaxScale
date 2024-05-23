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
import AlterEditor from '@wsModels/AlterEditor'
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import Worksheet from '@wsModels/Worksheet'
import store from '@/store'
import queryConnService from '@wsServices/queryConnService'
import { queryAndParseTblDDL } from '@/store/queryHelper'
import { NODE_TYPES, DDL_EDITOR_SPECS } from '@/constants/workspace'
import { getErrorsArr } from '@/utils/helpers'
import { t as typy } from 'typy'

async function queryTblCreationInfo(node) {
  const config = Worksheet.getters('activeRequestConfig')
  const { id: connId } = QueryConn.getters('activeQueryTabConn')
  const activeQueryTabId = QueryEditor.getters('activeQueryTabId')
  await queryConnService.enableSqlQuoteShowCreate({ connId, config })
  const schema = node.parentNameData[NODE_TYPES.SCHEMA]
  const [e, parsedTables] = await queryAndParseTblDDL({
    connId,
    targets: [{ tbl: node.name, schema }],
    config,
    charsetCollationMap: store.state.ddlEditor.charset_collation_map,
  })
  if (e) {
    AlterEditor.update({ where: activeQueryTabId, data: { is_fetching: false } })
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', { text: getErrorsArr(e), type: 'error' })
  } else {
    AlterEditor.update({
      where: activeQueryTabId,
      data: {
        is_fetching: false,
        active_node: node,
        active_spec: DDL_EDITOR_SPECS.COLUMNS,
        data: typy(parsedTables, '[0]').safeObjectOrEmpty,
      },
    })
  }
}

export default { queryTblCreationInfo }