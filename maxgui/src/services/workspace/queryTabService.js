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
import InsightViewer from '@wsModels/InsightViewer'
import TblEditor from '@wsModels/TblEditor'
import TxtEditor from '@wsModels/TxtEditor'
import DdlEditor from '@wsModels/DdlEditor'
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import QueryResult from '@wsModels/QueryResult'
import QueryTab from '@wsModels/QueryTab'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import Worksheet from '@wsModels/Worksheet'
import connection from '@/api/sql/connection'
import store from '@/store'
import queryConnService from '@wsServices/queryConnService'
import { QUERY_TAB_TYPE_MAP } from '@/constants/workspace'
import { uuidv1, tryAsync } from '@/utils/helpers'

const { TBL_EDITOR, INSIGHT_VIEWER, SQL_EDITOR, DDL_EDITOR } = QUERY_TAB_TYPE_MAP
/**
 * If a record is deleted, then the corresponding records in the child
 * tables will be automatically deleted
 * @param {String|Function} payload - either a queryTab id or a callback function that return Boolean (filter)
 */
function cascadeDelete(payload) {
  const entityIds = QueryTab.filterEntity(QueryTab, payload).map((entity) => entity.id)
  entityIds.forEach((id) => {
    QueryTab.delete(id) // delete itself
    // delete record in its the relational tables
    QueryTabTmp.delete(id)
    InsightViewer.delete(id)
    TblEditor.delete(id)
    TxtEditor.delete(id)
    DdlEditor.delete(id)
    store.dispatch('fileSysAccess/deleteFileHandleData', id)
    QueryResult.delete(id)
    QueryConn.delete((c) => c.query_tab_id === id)
    // Auto select the last QueryTab as the active one
    if (QueryEditor.getters('activeQueryTabId') === id) {
      const lastQueryTab = QueryTab.query().last()
      if (lastQueryTab)
        QueryEditor.update({
          where: lastQueryTab.query_editor_id,
          data: { active_query_tab_id: lastQueryTab.id },
        })
    }
  })
}

/**
 * Refresh non-key and non-relational fields of an entity and its relations
 * @param {String|Function} payload - either a QueryTab id or a callback function that return Boolean (filter)
 */
function cascadeRefresh(payload) {
  const entityIds = QueryTab.filterEntity(QueryTab, payload).map((entity) => entity.id)
  entityIds.forEach((id) => {
    const target = QueryTab.find(id)
    if (target) {
      QueryTab.refreshName(id)
      // refresh its relations
      QueryTabTmp.refresh(id)
      // Refresh all fields except sql
      if (target.type === SQL_EDITOR) {
        TxtEditor.refresh(id, ['sql'])
      } else {
        // If not SQL_EDITOR, change to it and delete other editor models
        QueryTab.update({ where: id, data: { type: SQL_EDITOR } })
        TxtEditor.insert({ data: { id } })
        QueryResult.insert({ data: { id } })
        TblEditor.delete(id)
        InsightViewer.delete(id)
      }
      QueryResult.refresh(id)
    }
  })
}

/**
 * Initialize a blank QueryTab and its mandatory relational entities
 * @param {object} param
 * @param {string} param.query_editor_id  - id of the QueryEditor has QueryTab being inserted
 * @param {string} [param.query_tab_id]
 * @param {string} [param.name]
 * @param {string} [param.type] - QUERY_TAB_TYPE_MAP values. default is SQL_EDITOR
 */
function insert({ query_editor_id, query_tab_id = uuidv1(), name = '', type }) {
  let tabName = 'Query Tab 1',
    count = 1
  const tabType = type || SQL_EDITOR
  const lastQueryTabOfWke = QueryTab.query()
    .where((t) => t.query_editor_id === query_editor_id)
    .last()
  if (lastQueryTabOfWke) {
    count = lastQueryTabOfWke.count + 1
    tabName = `Query Tab ${count}`
  }
  QueryTab.insert({
    data: { id: query_tab_id, count, name: name ? name : tabName, type: tabType, query_editor_id },
  })
  switch (tabType) {
    case TBL_EDITOR:
      TblEditor.insert({ data: { id: query_tab_id } })
      break
    case INSIGHT_VIEWER:
      InsightViewer.insert({ data: { id: query_tab_id } })
      break
    case SQL_EDITOR:
      TxtEditor.insert({ data: { id: query_tab_id } })
      QueryResult.insert({ data: { id: query_tab_id } })
      break
    case DDL_EDITOR:
      DdlEditor.insert({ data: { id: query_tab_id } })
      break
  }
  QueryTabTmp.insert({ data: { id: query_tab_id } })
  QueryEditor.update({ where: query_editor_id, data: { active_query_tab_id: query_tab_id } })
}

/**
 * Add a new queryTab to the provided QueryEditor id.
 * It uses the QueryEditor connection to clone into a new connection and bind it
 * to the queryTab being created.
 */
async function handleAdd(param) {
  const query_tab_id = uuidv1()
  insert({ query_tab_id, ...param })
  const queryEditorConn = QueryConn.getters('activeQueryEditorConn')
  // Clone the QueryEditor conn and bind it to the new queryTab
  if (queryEditorConn.id)
    await queryConnService.openQueryTabConn({ queryEditorConn, query_tab_id, schema: param.schema })
}

async function handleDelete(query_tab_id) {
  const config = Worksheet.getters('activeRequestConfig')
  const { id } = queryConnService.findQueryTabConn(query_tab_id)
  if (id) await tryAsync(connection.delete({ id, config }))
  cascadeDelete(query_tab_id)
}

async function refreshLast(query_tab_id) {
  cascadeRefresh(query_tab_id)
  QueryTab.update({ where: query_tab_id, data: { name: 'Query Tab 1', count: 1 } })
  // cascadeRefresh won't refresh sql but refresh does
  TxtEditor.refresh(query_tab_id)
  store.dispatch('fileSysAccess/deleteFileHandleData', query_tab_id)
}

export default { cascadeDelete, cascadeRefresh, insert, handleAdd, handleDelete, refreshLast }
