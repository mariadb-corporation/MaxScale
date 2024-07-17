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
import * as sqlLimiter from '@/utils/sqlLimiter'

function testInjectLimitOffset({ sql, shouldReplace, expectedStatement }) {
  const [, statementClasses] = sqlLimiter.getStatementClasses(sql)
  const [enforceErr, statement] = sqlLimiter.enforceLimitOffset({
    statementClass: statementClasses[0],
    shouldReplace,
    limitNumber: 1000,
    offsetNumber: 10,
  })
  if (enforceErr) {
    expect(enforceErr).toBeInstanceOf(Error)
    throw enforceErr
  } else expect(statement).toStrictEqual(expectedStatement)
}

function testEnforceNoLimit({ sql, expectedText }) {
  const [, statementClasses] = sqlLimiter.getStatementClasses(sql)
  const [, statement] = sqlLimiter.enforceNoLimit(statementClasses[0])
  expect(statement).toStrictEqual({
    text: expectedText,
    limit: 0,
    offset: undefined,
    type: 'select',
  })
}

describe('sqlLimiter', () => {
  it('Should return error object and undefined statement', () => {
    assert.throws(() => testInjectLimitOffset({ sql: 'SELECT * FROM something limit' }))
  })

  it('Limit and offset are not defined', () => {
    const statement = {
      text: 'SELECT * FROM something limit 1000 offset 10',
      limit: 1000,
      offset: 10,
      type: 'select',
    }
    testInjectLimitOffset({ sql: 'SELECT * FROM something', expectedStatement: statement })
    testEnforceNoLimit({ sql: statement.text, expectedText: 'SELECT * FROM something' })
  })

  it('Both limit and offset are defined', () => {
    const statement = {
      text: 'SELECT * FROM something limit 1000 offset 1',
      limit: 1000,
      offset: 1,
      type: 'select',
    }
    testInjectLimitOffset({
      sql: 'SELECT * FROM something limit 10000 offset 1',
      expectedStatement: statement,
    })
    testEnforceNoLimit({ sql: statement.text, expectedText: 'SELECT * FROM something' })
  })

  it('Limit offset, row_count syntax', () => {
    const statement = {
      text: 'SELECT * FROM something limit 5, 1000',
      limit: 1000,
      offset: 5,
      type: 'select',
    }
    testInjectLimitOffset({
      sql: `SELECT * FROM something limit 5, 10000`,
      expectedStatement: statement,
    })
    testEnforceNoLimit({ sql: statement.text, expectedText: 'SELECT * FROM something' })
  })

  it('Handles subquery', () => {
    const statement = {
      text: 'SELECT * FROM ( select something OFFSET 1 ROW ) limit 1000 offset 10',
      limit: 1000,
      offset: 10,
      type: 'select',
    }
    testInjectLimitOffset({
      sql: `SELECT * FROM ( select something OFFSET 1 ROW )`,
      expectedStatement: statement,
    })
    testEnforceNoLimit({
      sql: statement.text,
      expectedText: 'SELECT * FROM ( select something OFFSET 1 ROW )',
    })
  })

  it('Handles line-break', () => {
    const statement = {
      text: 'SELECT * FROM something limit 1000 offset 10',
      limit: 1000,
      offset: 10,
      type: 'select',
    }
    testInjectLimitOffset({
      sql: 'SELECT * FROM something;\n',
      expectedStatement: {
        text: 'SELECT * FROM something limit 1000 offset 10',
        limit: 1000,
        offset: 10,
        type: 'select',
      },
    })
    testEnforceNoLimit({ sql: statement.text, expectedText: 'SELECT * FROM something' })
  })

  it('Handles existing offset with enforce mode', () => {
    testInjectLimitOffset({
      sql: 'SELECT * FROM something limit 1000 offset 0',
      shouldReplace: true,
      expectedStatement: {
        text: 'SELECT * FROM something limit 1000 offset 10',
        limit: 1000,
        offset: 10,
        type: 'select',
      },
    })
  })

  it('Handles existing offset with enforce mode', () => {
    testInjectLimitOffset({
      sql: 'SELECT * FROM something limit 0, 5000',
      shouldReplace: true,
      expectedStatement: {
        text: 'SELECT * FROM something limit 10, 1000',
        limit: 1000,
        offset: 10,
        type: 'select',
      },
    })
  })

  it('getStatementClasses should split multi statements as expected', () => {
    const [, statementClasses] = sqlLimiter.getStatementClasses(
      `SELECT * FROM t1; SELECT * FROM t2; /* This is a comment */`
    )
    expect(statementClasses.length).toBe(3)
    expect(statementClasses.map((stmtClass) => stmtClass.toString())).toStrictEqual([
      'SELECT * FROM t1;',
      ' SELECT * FROM t2;',
      ' /* This is a comment */',
    ])
  })
})
