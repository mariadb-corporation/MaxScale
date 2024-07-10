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
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import Worksheet from '@wsModels/Worksheet'
import queries from '@/api/sql/queries'
import store from '@/store'
import queryConnService from '@wsServices/queryConnService'
import prefAndStorageService from '@wsServices/prefAndStorageService'
import { QUERY_MODES, QUERY_LOG_TYPES, QUERY_CANCELED } from '@/constants/workspace'
import { tryAsync, getErrorsArr, lodash } from '@/utils/helpers'
import { t as typy } from 'typy'
import { addStatementInfo } from '@/utils/queryUtils'

function setField(obj, path, values) {
  Object.keys(values).forEach((key) => {
    lodash.set(obj, [...path, key], values[key])
  })
}

/**
 * @param {object} param
 * @param {string} param.connId - Connection ID for querying
 * @param {string} param.sql - SQL query string to be executed
 * @param {array.<string>} param.path - Field path for storing data to QueryTabTmp. e.g. query_results or insight_data.tables
 * @param {number} param.maxRows - max_rows
 * @param {function} param.getStatementCb - Callback function to get the statement object
 * @param {string} param.queryName - Name of the query.
 * @param {string} param.queryType - Type of the query. e.g. QUERY_LOG_TYPES.ACTION_LOGS
 * @param {function} [param.successCb] - Callback function to handle successful query execution.
 * @param {boolean} [param.abortable] - Indicates if the query is abortable.
 * @returns {Promise<void>}
 */
async function query({
  connId,
  sql,
  path,
  maxRows,
  getStatementCb,
  queryName,
  queryType,
  successCb,
  abortable = false,
}) {
  const config = Worksheet.getters('activeRequestConfig')
  const activeQueryTabId = QueryEditor.getters('activeQueryTabId')
  const { meta: { name: connection_name } = {} } = QueryConn.find(connId) || {}
  const request_sent_time = new Date().valueOf()
  let abortController
  if (abortable) {
    abortController = new AbortController()
    store.commit('queryResultsMem/UPDATE_ABORT_CONTROLLER_MAP', {
      id: activeQueryTabId,
      value: abortController,
    })
  }

  QueryTabTmp.update({
    where: activeQueryTabId,
    data: (obj) => setField(obj, path, { request_sent_time, total_duration: 0, is_loading: true }),
  })

  let [e, res] = await tryAsync(
    queries.post({
      id: connId,
      body: { sql, max_rows: maxRows },
      config: abortable ? { ...config, signal: abortController.signal } : config,
    })
  )
  const now = new Date().valueOf()
  const total_duration = ((now - request_sent_time) / 1000).toFixed(4)

  if (e && !abortable)
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', { text: getErrorsArr(e), type: 'error' })
  else await typy(successCb).safeFunction(e, res)

  if (abortable && typy(QueryTabTmp.find(activeQueryTabId), 'has_kill_flag').safeBoolean) {
    // If the KILL command was sent for the query is being run, the query request is aborted
    QueryTabTmp.update({ where: activeQueryTabId, data: { has_kill_flag: false } })
    res = { data: { data: { attributes: { results: [{ message: QUERY_CANCELED }], sql } } } }
  }

  res = addStatementInfo({ res, getStatementCb })

  QueryTabTmp.update({
    where: activeQueryTabId,
    data: (obj) =>
      setField(obj, path, {
        data: Object.freeze(typy(res.data.data).safeObjectOrEmpty),
        total_duration: parseFloat(total_duration),
        is_loading: false,
      }),
  })

  prefAndStorageService.pushQueryLog({
    startTime: request_sent_time,
    name: queryName,
    sql,
    res,
    connection_name,
    queryType,
  })

  if (abortable) store.commit('queryResultsMem/DELETE_ABORT_CONTROLLER', activeQueryTabId)
}

/**
 * @param {object} param
 * @param {String} param.qualified_name - Table id (database_name.table_name).
 * @param {String} param.query_mode - a key in QUERY_MODES. Either PRVW_DATA or PRVW_DATA_DETAILS
 */
async function queryPrvw({ qualified_name, query_mode }) {
  const { id } = QueryConn.getters('activeQueryTabConn')
  let path, sql
  switch (query_mode) {
    case QUERY_MODES.PRVW_DATA:
      sql = `SELECT * FROM ${qualified_name} LIMIT 1000`
      path = ['prvw_data']
      break
    case QUERY_MODES.PRVW_DATA_DETAILS:
      sql = `DESCRIBE ${qualified_name}`
      path = ['prvw_data_details']
      break
  }
  await query({
    connId: id,
    sql,
    path,
    maxRows: 1000,
    getStatementCb: () => ({ text: sql }),
    queryName: sql,
    queryType: QUERY_LOG_TYPES.ACTION_LOGS,
  })
}

async function queryProcessList() {
  const { id } = QueryConn.getters('activeQueryTabConn')
  const { query_row_limit } = store.state.prefAndStorage
  const sql = `SELECT * FROM INFORMATION_SCHEMA.PROCESSLIST LIMIT ${query_row_limit}`
  await query({
    connId: id,
    sql,
    path: ['process_list'],
    maxRows: query_row_limit,
    getStatementCb: () => ({ text: sql }),
    queryName: sql,
    queryType: QUERY_LOG_TYPES.ACTION_LOGS,
  })
}

/**
 * @param {object} param
 * @param {array} param.statements - Array of statement objects.
 * @param {string} param.sql - SQL string joined from statements.
 */
async function executeSQL({ statements, sql }) {
  const { id } = QueryConn.getters('activeQueryTabConn')
  const { query_row_limit } = store.state.prefAndStorage
  await query({
    connId: id,
    sql,
    path: ['query_results'],
    maxRows: query_row_limit,
    /* TODO: The current mapping doesn't guarantee correctness since the statements may be
     * split incorrectly. Either the API supports an array of statements instead of a single SQL string,
     * or the GUI sends an array of requests
     */
    getStatementCb: (i) => statements[i],
    queryName: sql,
    queryType: QUERY_LOG_TYPES.USER_LOGS,
    abortable: true,
    successCb: async (e, res) => {
      if (!e && res && sql.match(/(use|drop database)\s/i)) await queryConnService.updateActiveDb()
    },
  })
}

async function queryInsightData({ connId, sql, spec }) {
  const { query_row_limit } = store.state.prefAndStorage
  await query({
    connId,
    sql,
    path: ['insight_data', spec],
    maxRows: query_row_limit,
    getStatementCb: () => ({ text: sql }),
    queryName: sql,
    queryType: QUERY_LOG_TYPES.ACTION_LOGS,
  })
}

/**
 * This action uses the current active QueryEditor connection to send
 * KILL QUERY thread_id
 */
async function killQuery() {
  const config = Worksheet.getters('activeRequestConfig')
  const activeQueryTabConn = QueryConn.getters('activeQueryTabConn')
  const activeQueryTabId = QueryEditor.getters('activeQueryTabId')
  const queryEditorConn = QueryConn.getters('activeQueryEditorConn')

  // abort the query first then send kill flag
  const abort_controller = store.getters['queryResultsMem/getAbortController'](activeQueryTabId)
  abort_controller.abort() // abort the running query

  QueryTabTmp.update({
    where: activeQueryTabId,
    data: { has_kill_flag: true },
  })
  const [e, res] = await tryAsync(
    queries.post({
      id: queryEditorConn.id,
      body: { sql: `KILL QUERY ${activeQueryTabConn.attributes.thread_id}` },
      config,
    })
  )
  if (!e) {
    const results = typy(res, 'data.data.attributes.results').safeArray
    if (typy(results, '[0].errno').isDefined)
      store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
        text: [
          'Failed to stop the query',
          ...Object.keys(results[0]).map((key) => `${key}: ${results[0][key]}`),
        ],
        type: 'error',
      })
  }
}

export default {
  queryPrvw,
  executeSQL,
  killQuery,
  queryProcessList,
  queryInsightData,
}
