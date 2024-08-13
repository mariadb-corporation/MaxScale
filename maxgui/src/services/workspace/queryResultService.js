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
import { genStatement } from '@/utils/sqlLimiter'
import { SNACKBAR_TYPE_MAP } from '@/constants'
import { QUERY_MODE_MAP, QUERY_LOG_TYPE_MAP, QUERY_CANCELED } from '@/constants/workspace'
import {
  tryAsync,
  getErrorsArr,
  lodash,
  immutableUpdate,
  getCurrentTimeStamp,
} from '@/utils/helpers'
import { t as typy } from 'typy'

function setField(obj, path, values) {
  Object.keys(values).forEach((key) => {
    lodash.set(obj, [...path, key], values[key])
  })
}

function getCanceledRes(statement) {
  return {
    data: { data: { attributes: { results: [{ message: QUERY_CANCELED, statement }] } } },
  }
}

/**
 * @param {object} param
 * @param {string} [param.connId] - Connection ID for querying. activeQueryTabConn is used if it is not defined.
 * @param {function} param.statement - a statement to be executed
 * @param {array.<string>} param.path - Field path for storing data to QueryTabTmp. e.g. query_results or insight_data.tables
 * @param {number} param.maxRows - max_rows
 * @param {string} param.queryType - Type of the query. e.g. QUERY_LOG_TYPE_MAP.ACTION_LOGS
 * @param {function} [param.successCb] - Callback function to handle successful query execution.
 * @param {object} [param.reqConfig] - request config
 * @returns {Promise<void>}
 */
async function query({
  connId,
  statement = {},
  path,
  maxRows,
  queryType,
  successCb,
  reqConfig = {},
}) {
  const config = Worksheet.getters('activeRequestConfig')
  const activeQueryTabId = QueryEditor.getters('activeQueryTabId')
  const conn = connId ? QueryConn.find(connId) : QueryConn.getters('activeQueryTabConn')
  const { meta: { name: connection_name } = {} } = conn || {}
  const { query_row_limit } = store.state.prefAndStorage

  const start_time = getCurrentTimeStamp()
  const sql = statement.text

  QueryTabTmp.update({
    where: activeQueryTabId,
    data: (obj) => setField(obj, path, { start_time, end_time: 0, is_loading: true }),
  })

  let [e, res] = await tryAsync(
    queries.post({
      id: conn.id,
      body: { sql, max_rows: typy(maxRows).isDefined ? maxRows : query_row_limit },
      config: { ...config, ...reqConfig },
    })
  )
  if (e) {
    if (typy(e, 'code').safeString === 'ERR_CANCELED') res = getCanceledRes(statement)
    else
      store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
        text: getErrorsArr(e),
        type: SNACKBAR_TYPE_MAP.ERROR,
      })
  } else {
    // add statement info
    res = immutableUpdate(res, {
      data: {
        data: {
          attributes: {
            results: {
              [0]: { $merge: { statement } },
            },
          },
        },
      },
    })
    await typy(successCb).safeFunction()
  }

  QueryTabTmp.update({
    where: activeQueryTabId,
    data: (obj) =>
      setField(obj, path, {
        data: Object.freeze(typy(res.data.data).safeObjectOrEmpty),
        end_time: getCurrentTimeStamp(),
        is_loading: false,
      }),
  })

  prefAndStorageService.pushQueryLog({
    startTime: start_time,
    name: sql,
    sql,
    res,
    connection_name,
    queryType,
  })
}

/**
 * @param {object} param
 * @param {string} param.qualified_name - Table id (database_name.table_name).
 * @param {string} param.query_mode - a key in QUERY_MODE_MAP. Either PRVW_DATA or PRVW_DATA_DETAILS
 * @param {object} [param.customStatement] - custom statement
 */
async function queryPrvw({ qualified_name, query_mode, customStatement }) {
  let path, sql, limit, type
  switch (query_mode) {
    case QUERY_MODE_MAP.PRVW_DATA:
      limit = 1000
      sql = `SELECT * FROM ${qualified_name} LIMIT ${limit}`
      path = ['prvw_data']
      type = 'select'
      break
    case QUERY_MODE_MAP.PRVW_DATA_DETAILS:
      sql = `DESCRIBE ${qualified_name}`
      path = ['prvw_data_details']
      type = 'describe'
      break
  }
  const statement = customStatement || genStatement({ text: sql, limit, type })
  await query({
    statement,
    maxRows: statement.limit,
    path,
    queryType: QUERY_LOG_TYPE_MAP.ACTION_LOGS,
  })
}

/**
 * @param {object} [customStatement] - custom statement
 */
async function queryProcessList(customStatement) {
  const limit = store.state.prefAndStorage.query_row_limit
  const statement =
    customStatement ||
    genStatement({
      text: `SELECT * FROM INFORMATION_SCHEMA.PROCESSLIST LIMIT ${limit}`,
      limit,
      type: 'select',
    })

  await query({
    statement,
    maxRows: statement.limit,
    path: ['process_list'],
    queryType: QUERY_LOG_TYPE_MAP.ACTION_LOGS,
  })
}

/**
 * @param {object} param
 * @param {object} param.statement - statement object
 * @param {array<string>} param.path
 * @param {boolean} [param.autoResetKillFlag=true] Automatically reset has_kill_flag after calling killQuery
 */
async function exeStatement({ statement, path, autoResetKillFlag = true }) {
  const activeQueryTabId = QueryEditor.getters('activeQueryTabId')
  if (typy(QueryTabTmp.find(activeQueryTabId), 'has_kill_flag').safeBoolean) {
    QueryTabTmp.update({
      where: activeQueryTabId,
      data: (obj) =>
        setField(obj, path, {
          data: Object.freeze(typy(getCanceledRes(statement), 'data.data').safeObjectOrEmpty),
          end_time: getCurrentTimeStamp(),
          is_loading: false,
        }),
    })
    if (autoResetKillFlag)
      QueryTabTmp.update({ where: activeQueryTabId, data: { has_kill_flag: false } })
  } else {
    const sql = statement.text
    const abortController = new AbortController()
    store.commit('queryResultsMem/UPDATE_ABORT_CONTROLLER_MAP', {
      id: activeQueryTabId,
      value: abortController,
    })
    await query({
      statement,
      maxRows: statement.limit,
      path,
      queryType: QUERY_LOG_TYPE_MAP.USER_LOGS,
      reqConfig: { signal: abortController.signal },
      successCb: async () => {
        if (sql.match(/(use|drop database)\s/i)) await queryConnService.updateActiveDb()
      },
    })
    store.commit('queryResultsMem/DELETE_ABORT_CONTROLLER', activeQueryTabId)
  }
}

/**
 * @param {array} statements - Array of statement objects.
 */
async function exeStatements(statements) {
  const activeQueryTabId = QueryEditor.getters('activeQueryTabId')

  QueryTabTmp.update({
    where: activeQueryTabId,
    data: (obj) =>
      setField(obj, ['query_results'], {
        start_time: getCurrentTimeStamp(),
        end_time: 0,
        is_loading: true,
        data: [],
        statements,
      }),
  })

  for (const [i, statement] of statements.entries()) {
    await exeStatement({ statement, path: ['query_results', 'data', i], autoResetKillFlag: false })
  }

  QueryTabTmp.update({
    where: activeQueryTabId,
    data: (obj) => {
      setField(obj, ['query_results'], {
        end_time: getCurrentTimeStamp(),
        is_loading: false,
      })
      obj.has_kill_flag = false
    },
  })
}

async function queryInsightData({ connId, statement, spec }) {
  await query({
    connId,
    statement,
    maxRows: statement.limit,
    path: ['insight_data', spec],
    queryType: QUERY_LOG_TYPE_MAP.ACTION_LOGS,
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
        type: SNACKBAR_TYPE_MAP.ERROR,
      })
  }
}

export default {
  query,
  queryPrvw,
  queryProcessList,
  queryInsightData,
  exeStatement,
  exeStatements,
  killQuery,
}
