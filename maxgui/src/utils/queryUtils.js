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
import sqlLimiter from 'sql-limiter'
import { formatDialect, mariadb } from 'sql-formatter'
import { capitalizeFirstLetter, immutableUpdate } from '@/utils/helpers'
import { t as typy } from 'typy'

/**
 * This function splits the query into statements accurately in most cases,
 * except compound statements, as it splits SQL text on the ";" terminator
 * @param {string} sql
 * @returns {string[]}
 */
export function splitQuery(sql) {
  return sqlLimiter.getStatements(sql)
}

/**
 * Format MariaDB SQL dialect.
 * The use of changing DELIMITER is not supported.
 * @param {string} v- SQL
 * @returns {string}
 */
export function formatSQL(v) {
  return formatDialect(v, { dialect: mariadb, tabWidth: 2, keywordCase: 'upper' })
}

const IDENTIFIED_PATTERN = /IDENTIFIED\s*/gim
const PWD_PATTERN = /['"][^'"]*['"]/

const IDENTIFIED_BY_PATTERN = new RegExp(
  '(\\b' + IDENTIFIED_PATTERN.source + 'BY(?:\\s*PASSWORD)?\\s*)' + PWD_PATTERN.source,
  'gim'
)
const IDENTIFIED_PLUGIN_PATTERN = new RegExp(
  '(\\b' + IDENTIFIED_PATTERN.source + '(VIA|WITH)\\s*\\w+\\s*(USING|AS)\\s*)' + PWD_PATTERN.source,
  'gim'
)
const PLUGIN_PWD_PATTERN = new RegExp(`PASSWORD\\s*\\(${PWD_PATTERN.source}`, 'gim')

/**
 * @param {string} query - SQL
 * @returns {string} SQL with password masked.
 */
export function maskQueryPwd(query) {
  if (query.match(IDENTIFIED_PATTERN) || query.match(PLUGIN_PWD_PATTERN))
    return query
      .replace(IDENTIFIED_BY_PATTERN, "$1'***'")
      .replace(PLUGIN_PWD_PATTERN, "PASSWORD('***'")
      .replace(IDENTIFIED_PLUGIN_PATTERN, `$1'***'`)
  return query
}

/**
 * @param {object} result - error query result
 * @returns {string}
 */
export function stringifyErrResult(result) {
  return Object.keys(result)
    .map((key) => `${capitalizeFirstLetter(key)}: ${result[key]}.`)
    .join(' ')
}

/**
 * Enforce limit, offset on SQL SELECT queries.
 * This function will split the query into statements and inject LIMIT, OFFSET to each statement.
 * Non SELECT queries will not be altered.
 * If existing limit exists, it will be lowered if it is larger than `limitNumber` specified.
 * If limit does not exist, it will be added.
 * If existing offset exists, it will not be altered.
 * @param {string} sql - sql text to limit
 * @param {number} limitNumber -- number to enforce for limit keyword
 * @param {number} offsetNumber -- offset number to enforce
 * @returns {Array<{text: string, offset?: number, limit?: number}}
 */
export function injectLimitOffset({ sql, limitNumber, offsetNumber }) {
  return sqlLimiter.getStatementClasses(sql).map((statement) => {
    const limit = statement.enforceLimit(['limit', 'fetch'], limitNumber)
    const offset = statement.enforceOffset(offsetNumber)
    return { text: sqlLimiter.removeTerminator(statement.toString().trim()), limit, offset }
  })
}

/**
 * Add statement info to each result object
 * @param {Object} params.res - response from /sql/:connId/queries
 * @param {Function} params.getStatementCb - Callback function that returns the statement for a given index.
 * @returns {object}
 */
export function addStatementInfo({ res, getStatementCb }) {
  return immutableUpdate(res, {
    data: {
      data: {
        attributes: {
          results: {
            $set: typy(res, 'data.data.attributes.results').safeArray.map((item, i) => ({
              ...item,
              statement: getStatementCb(i),
            })),
          },
        },
      },
    },
  })
}
