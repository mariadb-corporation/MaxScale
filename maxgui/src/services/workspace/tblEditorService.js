/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import TblEditor from '@wsModels/TblEditor'
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import Worksheet from '@wsModels/Worksheet'
import store from '@/store'
import queryConnService from '@wsServices/queryConnService'
import schemaInfoService from '@wsServices/schemaInfoService'
import { queryAndParseTblDDL } from '@/store/queryHelper'
import { SNACKBAR_TYPE_MAP } from '@/constants'
import { NODE_TYPE_MAP } from '@/constants/workspace'
import { getErrorsArr } from '@/utils/helpers'
import { t as typy } from 'typy'
import ddlTemplate from '@/utils/ddlTemplate'
import TableParser from '@/utils/TableParser'
import erdHelper from '@/utils/erdHelper'

/**
 * @param {object} param
 * @param {object} param.node - table node
 * @param {string} param.spec - default table structure spec. TABLE_STRUCTURE_SPEC_MAP
 */
async function loadTblStructureData({ node, spec }) {
  const config = Worksheet.getters('activeRequestConfig')
  const { id: connId } = QueryConn.getters('activeQueryTabConn')
  const activeQueryTabId = QueryEditor.getters('activeQueryTabId')
  /**
   * Enable sql_quote_show_create and fetch supplementary schema data to
   * support parsing the table creation script.
   */
  await queryConnService.enableSqlQuoteShowCreate({ connId, config })
  await schemaInfoService.querySuppData({ connId, config })

  const schema = node.parentNameData[NODE_TYPE_MAP.SCHEMA]
  const [e, parsedTables] = await queryAndParseTblDDL({
    connId,
    targets: [{ tbl: node.name, schema }],
    config,
    charsetCollationMap: store.state.schemaInfo.charset_collation_map,
  })

  if (e)
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
      text: getErrorsArr(e),
      type: SNACKBAR_TYPE_MAP.ERROR,
    })

  TblEditor.update({
    where: activeQueryTabId,
    data: {
      is_fetching: false,
      active_node: node,
      active_spec: spec,
      data: e ? {} : typy(parsedTables, '[0]').safeObjectOrEmpty,
    },
  })
}

/**
 * @param {string} schema - The name of the schema to have the new table
 */
async function genNewTable(schema) {
  const config = Worksheet.getters('activeRequestConfig')
  const { id: connId } = QueryConn.getters('activeQueryTabConn')
  const activeQueryTabId = QueryEditor.getters('activeQueryTabId')

  await schemaInfoService.querySuppData({ connId, config })

  const tableParser = new TableParser()
  const parsedTable = tableParser.parse({
    ddl: ddlTemplate.createTbl(`new_table`),
    schema,
    autoGenId: true,
  })

  TblEditor.update({
    where: activeQueryTabId,
    data: {
      is_fetching: false,
      data: erdHelper.genTblStructureData({
        parsedTable,
        charsetCollationMap: store.state.schemaInfo.charset_collation_map,
      }),
    },
  })
}

export default { loadTblStructureData, genNewTable }
