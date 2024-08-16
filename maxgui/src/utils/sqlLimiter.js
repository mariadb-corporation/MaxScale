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

/**
 * Filter out limit and offset and its values tokens
 * @param {Array<object>} tokens
 * @param {object} limitToken
 * @param {object} offsetToken
 * @returns {Array<object>} filtered tokens
 */
function filterLimitOffsetTokens(tokens, limitToken, offsetToken) {
  const filteredTokens = []
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

export function genStatement({ text = '', limit = undefined, offset = undefined, type = '' } = {}) {
  return { text, limit, offset, type }
}

/**
 * This function splits the SQL into statement classes parsed by sql-limiter
 * @param {string} sql
 * @returns {[Error|undefined, Class[]|undefined]}
 */
export function getStatementClasses(sql) {
  try {
    return [undefined, limiter.getStatementClasses(sql)]
  } catch (e) {
    return [e, undefined]
  }
}

/**
 * @param {object} numberToken - Either limit or offset number token
 * @returns {number|undefined} - Parsed integer value of the token or undefined
 */
function getParsedNumberToken(numberToken) {
  return numberToken ? parseInt(numberToken.value, 10) : undefined
}

/**
 * Enforce limit and offset for `select` statement
 * mode:
 * cap: If existing limit or offset exists, it will be lowered to match the provided values
 * inject: Injects limit or offset if they don't exist
 * replace: Replaces existing limit and offset values with the provided values or injects them if they don't exist.
 * @param {object} param
 * @param {Class} param.statementClass - a statement class, returned by getStatementClasses
 * @param {number} param.limit -- number to enforce for limit keyword
 * @param {number} [param.offset] -- offset number
 * @param {boolean} [param.mode] -- either inject, replace, cap
 * @returns {[Error|undefined, undefined|{text: string, offset: number|undefined, limit: number|undefined, type: string}]}
 */
export function enforceLimitOffset({ statementClass, limit, offset, mode = 'inject' }) {
  try {
    const { statementToken } = statementClass
    let statement, limitNumber, offsetNumber

    if (typy(statementToken, 'value').safeString === 'select') {
      statementClass.enforceLimit(['limit', 'fetch'], limit, mode)
      if (typy(offset).isDefined) statementClass.enforceOffset(offset, mode)
      // after enforcing, get the values
      offsetNumber = getParsedNumberToken(statementClass.findOffsetNumberToken())
      limitNumber = getParsedNumberToken(statementClass.findLimitNumberToken(['limit', 'fetch']))
    }
    // After enforcing, output the statement to string
    const text = limiter.removeTerminator(statementClass.toString().trim())
    /**
     * sql-limiter treats the line-break after the last delimiter as a statement, so
     * text could be empty after trimming.
     */
    if (text)
      statement = genStatement({
        text,
        limit: limitNumber,
        offset: offsetNumber,
        type: limiter.getStatementType(text),
      })
    return [undefined, statement]
  } catch (e) {
    return [e, undefined]
  }
}

/**
 * @param {class} statementClass - select statement
 * @returns {[Error|undefined, undefined|{text: string, offset: undefined, limit: 0, type: 'select'}]}
 */
export function enforceNoLimit(statementClass) {
  try {
    let statement
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
      statement = genStatement({
        text,
        limit: 0, // for api max_rows field which indicates no limit
        offset: undefined,
        type: 'select',
      })
    return [undefined, statement]
  } catch (e) {
    return [e, undefined]
  }
}
