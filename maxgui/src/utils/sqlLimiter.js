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
import limiter from 'sql-limiter'
import { findParenLevelToken } from 'sql-limiter/src/token-utils'
import { lodash } from '@/utils/helpers'
import { t as typy } from 'typy'
import { logger } from '@/plugins/logger'

/**
 * This function splits the query into statements accurately in most cases,
 * except compound statements, as it splits SQL text on the ";" delimiter
 * @param {string} sql
 * @returns {string[]}
 */
export function splitSQL(sql) {
  return limiter.getStatements(sql)
}

/* TODO: Show a meaningful error to the user if injecting a limit or enforcing no limit fails. */

/**
 * Enforce limit, offset on SQL SELECT queries.
 * This function will split the query into statements and inject LIMIT, OFFSET to each statement.
 * Non SELECT queries will not be altered.
 * If existing limit exists, it will be lowered if it is larger than `limitNumber` specified.
 * If limit does not exist, it will be added.
 * If existing offset exists, it will not be altered unless shouldEnforceOffset is true
 * @param {object} param
 * @param {string} param.sql - sql text to limit
 * @param {number} param.limitNumber -- number to enforce for limit keyword
 * @param {number} param.offsetNumber -- offset number to inject
 * @param {boolean} param.shouldReplace -- If true, it injects or replaces the existing limit and offset values
 * @returns {Array<{text: string, offset?: number, limit?: number, type: string}}
 */
export function enforceLimitOffset({
  sql,
  multi = false,
  limitNumber,
  offsetNumber,
  shouldReplace = false,
}) {
  try {
    const statements = limiter.getStatementClasses(sql).reduce((acc, statement) => {
      const limit = statement.enforceLimit(['limit', 'fetch'], limitNumber, shouldReplace)
      const offset = statement.injectOffset(offsetNumber, shouldReplace)
      const text = limiter.removeTerminator(statement.toString().trim())
      /**
       * sql-limiter treats the line-break after the last delimiter as a statement, so
       * text could be empty after trimming.
       */
      if (text) acc.push({ text, limit, offset, type: limiter.getStatementType(text) })
      return acc
    }, [])
    return multi ? statements : typy(statements, '[0]').safeObject
  } catch (e) {
    logger.error(e)
  }
}

/**
 * Filter out limit and offset and its values tokens
 * @param {Array<object>} tokens
 * @param {object} limitToken
 * @param {object} offsetToken
 * @returns {Array<object>} filtered tokens
 */
function filterLimitOffsetTokens(tokens, limitToken, offsetToken) {
  let filteredTokens = []
  let skipMode = false
  let removingLimit = false
  let removingOffset = false

  for (const token of tokens) {
    if (lodash.isEqual(token, limitToken)) {
      skipMode = true
      removingLimit = true
      continue
    }

    if (lodash.isEqual(token, offsetToken)) {
      skipMode = true
      removingOffset = true
      continue
    }

    if (skipMode) {
      // Retain comments and whitespace
      if (token.type === 'whitespace' || token.type === 'comment') {
        filteredTokens.push(token)
        continue
      }
      if (removingLimit && token.type === 'comma') continue
      if ((token.type === 'number' || token.type === 'comma') && (removingLimit || removingOffset))
        continue

      skipMode = false
      removingLimit = false
      removingOffset = false
    }

    filteredTokens.push(token)
  }

  return filteredTokens
}

function findParenLevelKeywordToken({ tokens, startIndex, keyword }) {
  return findParenLevelToken(
    tokens,
    startIndex,
    (token) => token.type === 'keyword' && token.value === keyword
  )
}

/**
 * @param {object} statement - select statement
 * @returns {object} statement with no limit and offset
 */
export function enforceNoLimit(statement) {
  try {
    const statements = limiter.getStatementClasses(statement.text).reduce((acc, statementClass) => {
      const { statementToken, tokens } = statementClass
      if (statementToken) {
        const limitToken = findParenLevelKeywordToken({
          tokens,
          startIndex: statementToken.index,
          keyword: 'limit',
        })
        const offsetToken = findParenLevelKeywordToken({
          tokens,
          startIndex: statementToken.index,
          keyword: 'offset',
        })
        if (limitToken)
          statementClass.tokens = filterLimitOffsetTokens(tokens, limitToken, offsetToken)
      }
      const text = limiter.removeTerminator(statementClass.toString().trim())
      if (text)
        acc.push({
          ...statement,
          text,
          limit: 0, // for api max_rows field which indicates no limit
          offset: undefined,
        })
      return acc
    }, [])
    return typy(statements, '[0]').safeObject
  } catch (e) {
    logger.error(e)
  }
}
