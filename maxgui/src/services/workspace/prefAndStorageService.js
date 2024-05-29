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
import store from '@/store'
import { globalI18n as i18n } from '@/plugins/i18n'
import { dateFormat, daysDiff, addDaysToNow } from '@/utils/helpers'
import { t as typy } from 'typy'
import { QUERY_CANCELED } from '@/constants/workspace'
import { maskQueryPwd, queryResErrToStr } from '@/utils/queryUtils'
import { logger } from '@/plugins/logger'

/**
 * @param {Number} payload.startTime - time when executing the query
 * @param {String} payload.connection_name - connection_name
 * @param {String} payload.name - name of the query, required when queryType is ACTION_LOGS
 * @param {String} payload.sql - sql
 * @param {Object} payload.res - query response
 * @param {String} payload.queryType - query type in QUERY_LOG_TYPES
 */
function pushQueryLog({ startTime, connection_name, name, sql, res, queryType }) {
  try {
    const maskedQuery = maskQueryPwd(sql)
    const { execution_time, results } = typy(res, 'data.data.attributes').safeObject

    let resultData = {}
    let resSetCount = 0
    let resCount = 0
    for (const res of results) {
      const { data, message = '', errno } = res
      const isQueryCanceled = message === QUERY_CANCELED

      if (isQueryCanceled) {
        resultData[`INTERRUPT`] = message
      } else if (data) {
        ++resSetCount
        resultData[`Result set ${resSetCount}`] = `${data.length} rows in set.`
      } else if (typy(errno).isNumber) {
        resultData[`Error`] = queryResErrToStr(res)
      } else {
        ++resCount
        resultData[`Result ${resCount}`] = `${res.affected_rows} rows affected.`
      }
    }

    let response = ''
    Object.keys(resultData).forEach((key) => {
      response += `${key}: ${resultData[key]} \n`
    })
    let action = {
      name: maskedQuery, // if no name is defined, use sql as name
      response,
      type: queryType,
    }
    // if query is aborted/canceled, there is no execution_time
    if (typy(execution_time).isNumber) action.execution_time = execution_time.toFixed(4)

    if (name) {
      action.sql = maskedQuery
      action.name = name
    }
    store.commit('prefAndStorage/UPDATE_QUERY_HISTORY', {
      payload: {
        date: startTime, // Unix time
        connection_name,
        time: dateFormat({ value: startTime, formatType: 'HH:mm:ss' }),
        action,
      },
    })
  } catch (e) {
    logger.error(e)
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
      text: [i18n.t('errors.persistentStorage')],
      type: 'error',
    })
  }
}

function saveQuerySnippet({ date, name, sql }) {
  try {
    store.commit('prefAndStorage/UPDATE_QUERY_SNIPPETS', {
      payload: {
        date, // Unix time
        time: dateFormat({ value: date, formatType: 'HH:mm:ss' }),
        name,
        sql: maskQueryPwd(sql),
      },
    })
  } catch (e) {
    logger.error(e)
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
      text: [i18n.t('errors.persistentStorage')],
      type: 'error',
    })
  }
}

function autoClearQueryHistory() {
  if (daysDiff(typy(store, 'state.prefAndStorage.query_history_expired_time').safeNumber) <= 0) {
    store.commit('prefAndStorage/SET_QUERY_HISTORY', [])
    store.commit('prefAndStorage/SET_QUERY_HISTORY_EXPIRED_TIME', addDaysToNow(30))
  }
}

export default { pushQueryLog, saveQuerySnippet, autoClearQueryHistory }
