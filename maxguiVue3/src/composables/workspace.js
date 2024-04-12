/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import connection from '@/api/sql/connection'
import queries from '@/api/sql/queries'
import { tryAsync, quotingIdentifier } from '@/utils/helpers'
import { t as typy } from 'typy'
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import EtlTaskTmp from '@wsModels/EtlTaskTmp'
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import QueryEditorTmp from '@wsModels/QueryEditorTmp'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import Worksheet from '@wsModels/Worksheet'
import WorksheetTmp from '@wsModels/WorksheetTmp'
import { QUERY_LOG_TYPES } from '@/constants/workspace'

/**
 * Initialize entities that will be kept only in memory for all worksheets and queryTabs
 */
function initMemEntities() {
  const worksheets = Worksheet.all()
  worksheets.forEach((w) => {
    WorksheetTmp.insert({ data: { id: w.id } })
    if (w.query_editor_id) {
      const queryEditor = QueryEditor.query()
        .where('id', w.query_editor_id)
        .with('queryTabs')
        .first()
      QueryEditorTmp.insert({ data: { id: queryEditor.id } })
      queryEditor.queryTabs.forEach((t) => QueryTabTmp.insert({ data: { id: t.id } }))
    } else if (w.etl_task_id) EtlTaskTmp.insert({ data: { id: w.etl_task_id } })
    else if (w.erd_task_id) ErdTaskTmp.insert({ data: { id: w.erd_task_id } })
  })
}

function initEntities() {
  if (Worksheet.all().length === 0) Worksheet.dispatch('insertBlankWke')
  else initMemEntities()
}

export function useInitWorkspace() {
  const store = useStore()
  return async () => {
    initEntities()
    await store.dispatch('fileSysAccess/initStorage')
  }
}

/**
 * @param {object} data - proxy object
 */
export function useCommonResSetAttrs(data) {
  const isLoading = computed(() => typy(data.value, 'is_loading').safeBoolean)
  const requestSentTime = computed(() => typy(data.value, 'request_sent_time').safeNumber)
  const execTime = computed(() => {
    if (isLoading.value) return -1
    const execution_time = typy(data.value, 'data.attributes.execution_time').safeNumber
    if (execution_time) return parseFloat(execution_time.toFixed(4))
    return 0
  })
  const totalDuration = computed(() => typy(data.value, 'total_duration').safeNumber)
  return {
    isLoading,
    requestSentTime,
    execTime,
    totalDuration,
  }
}

export function useFetchOdbcDrivers() {
  const data = ref([])
  return {
    fetch: async () => {
      const config = Worksheet.getters('activeRequestConfig')
      const [e, res] = await tryAsync(connection.getDrivers(config))
      if (!e && res.status === 200) data.value = res.data.data
    },
    data,
  }
}

export function useExeStatement() {
  const store = useStore()
  const { t } = useI18n()
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
  return async ({ connId, sql, action, showSnackbar = true }) => {
    const config = Worksheet.getters('activeRequestConfig')
    const { meta: { name: connection_name } = {} } = QueryConn.find(connId)
    const request_sent_time = new Date().valueOf()
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
      store.commit('mxsWorkspace/SET_EXEC_SQL_DLG', {
        ...store.state.mxsWorkspace.exec_sql_dlg,
        result: { data: typy(res, 'data.data.attributes').safeObject, error },
      })
      let queryAction
      if (error) queryAction = t('errors.failedToExeAction', { action })
      else {
        queryAction = t('success.exeAction', { action })
        if (showSnackbar)
          store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', { text: [queryAction], type: 'success' })
      }
      store.dispatch('prefAndStorage/pushQueryLog', {
        startTime: request_sent_time,
        name: queryAction,
        sql,
        res,
        connection_name,
        queryType: QUERY_LOG_TYPES.ACTION_LOGS,
      })
    }
  }
}

export function useExeDdlScript() {
  const store = useStore()
  const exeStatement = useExeStatement()
  /**
   * @param {string} param.connId - connection id
   * @param {boolean} [param.isCreating] - is creating a new table
   * @param {string} [param.schema] - schema name
   * @param {string} [param.name] - table name
   * @param {string} [param.actionName] - action name
   * @param {function} param.successCb - success callback function
   */
  return async ({ connId, isCreating = false, schema, name, successCb, actionName = '' }) => {
    let action
    if (actionName) action = actionName
    else {
      const targetObj = `${quotingIdentifier(schema)}.${quotingIdentifier(name)}`
      action = `Apply changes to ${targetObj}`
      if (isCreating) action = `Create ${targetObj}`
    }

    await exeStatement({ connId, sql: store.state.mxsWorkspace.exec_sql_dlg.sql, action })
    if (!store.getters['mxsWorkspace/isExecFailed']) await typy(successCb).safeFunction()
  }
}
