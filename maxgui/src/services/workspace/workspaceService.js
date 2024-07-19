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
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import EtlTaskTmp from '@wsModels/EtlTaskTmp'
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import QueryEditorTmp from '@wsModels/QueryEditorTmp'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import Worksheet from '@wsModels/Worksheet'
import WorksheetTmp from '@wsModels/WorksheetTmp'
import worksheetService from '@wsServices/worksheetService'
import queries from '@/api/sql/queries'
import store from '@/store'
import prefAndStorageService from '@wsServices/prefAndStorageService'
import { globalI18n as i18n } from '@/plugins/i18n'
import { tryAsync, quotingIdentifier } from '@/utils/helpers'
import { t as typy } from 'typy'
import { QUERY_LOG_TYPES } from '@/constants/workspace'

/**
 * Initialize entities that will be kept only in memory for all worksheets and queryTabs
 */
function initMemEntities() {
  const worksheets = Worksheet.all()
  worksheets.forEach((w) => {
    if (!WorksheetTmp.find(w.id)) WorksheetTmp.insert({ data: { id: w.id } })
    if (w.query_editor_id) {
      const queryEditor = QueryEditor.query()
        .where('id', w.query_editor_id)
        .with('queryTabs')
        .first()
      if (!QueryEditorTmp.find(w.query_editor_id))
        QueryEditorTmp.insert({ data: { id: queryEditor.id } })
      queryEditor.queryTabs.forEach((t) => {
        if (!QueryTabTmp.find(t.id)) QueryTabTmp.insert({ data: { id: t.id } })
      })
    } else if (w.etl_task_id && !EtlTaskTmp.find(w.etl_task_id))
      EtlTaskTmp.insert({ data: { id: w.etl_task_id } })
    else if (w.erd_task_id && !ErdTaskTmp.find(w.erd_task_id))
      ErdTaskTmp.insert({ data: { id: w.erd_task_id } })
  })
}

function initEntities() {
  if (Worksheet.all().length === 0) worksheetService.insertBlank()
  else initMemEntities()
}

async function init() {
  initEntities()
  await store.dispatch('fileSysAccess/initStorage')
}

/**
 * This action is used to execute statement or statements.
 * Since users are allowed to modify the auto-generated SQL statement,
 * they can add more SQL statements after or before the auto-generated statement
 * which may receive error. As a result, the action log still log it as a failed action.
 * @param {string} param.connId - connection id
 * @param {String} payload.sql - sql to be executed
 * @param {String} payload.action - action name. e.g. DROP TABLE table_name
 * @param {Boolean} [payload.showSnackbar] - show successfully snackbar message
 */
async function exeStatement({ connId, sql, action, showSnackbar = true }) {
  const config = Worksheet.getters('activeRequestConfig')
  const { meta: { name: connection_name } = {} } = QueryConn.find(connId)
  const start_time = new Date().valueOf()
  let error = null
  const [e, res] = await tryAsync(
    queries.post({
      id: connId,
      body: { sql, max_rows: store.state.prefAndStorage.query_row_limit },
      config,
    })
  )
  if (!e) {
    const results = typy(res, 'data.data.attributes.results').safeArray
    const errMsgs = results.filter((res) => typy(res, 'errno').isDefined)
    // if multi statement mode, it'll still return only an err msg obj
    if (errMsgs.length) error = errMsgs[0]
    store.commit('workspace/SET_EXEC_SQL_DLG', {
      ...store.state.workspace.exec_sql_dlg,
      result: { data: typy(res, 'data.data.attributes').safeObject, error },
    })
    let queryAction
    if (error) queryAction = i18n.t('errors.failedToExeAction', { action })
    else {
      queryAction = i18n.t('success.exeAction', { action })
      if (showSnackbar)
        store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', { text: [queryAction], type: 'success' })
    }
    prefAndStorageService.pushQueryLog({
      startTime: start_time,
      name: queryAction,
      sql,
      res,
      connection_name,
      queryType: QUERY_LOG_TYPES.ACTION_LOGS,
    })
  }
}

/**
 * @param {string} param.connId - connection id
 * @param {boolean} [param.isCreating] - is creating a new table
 * @param {string} [param.schema] - schema name
 * @param {string} [param.name] - table name
 * @param {string} [param.actionName] - action name
 * @param {function} param.successCb - success callback function
 */
async function exeDdlScript({
  connId,
  isCreating = false,
  schema,
  name,
  successCb,
  actionName = '',
}) {
  let action
  if (actionName) action = actionName
  else {
    const targetObj = `${quotingIdentifier(schema)}.${quotingIdentifier(name)}`
    action = `Apply changes to ${targetObj}`
    if (isCreating) action = `Create ${targetObj}`
  }
  await exeStatement({ connId, sql: store.state.workspace.exec_sql_dlg.sql, action })
  if (!store.getters['workspace/isExecFailed']) await typy(successCb).safeFunction()
}

export default { init, exeStatement, exeDdlScript }
