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

function testInjectLimitOffset({ sql, multi, shouldReplace, expected }) {
  expect(
    sqlLimiter.enforceLimitOffset({
      sql,
      multi,
      shouldReplace,
      limitNumber: 1000,
      offsetNumber: 10,
    })
  ).toStrictEqual(expected)
}

function testEnforceNoLimit({ statement, expectedText }) {
  expect(sqlLimiter.enforceNoLimit(statement)).toStrictEqual({
    text: expectedText,
    limit: 0,
    offset: undefined,
    type: 'select',
  })
}

describe('sqlLimiter', () => {
  it('Limit and offset are not defined', () => {
    const statement = {
      text: 'SELECT * FROM something limit 1000 offset 10',
      limit: 1000,
      offset: 10,
      type: 'select',
    }
    testInjectLimitOffset({ sql: 'SELECT * FROM something', expected: statement })
    testEnforceNoLimit({ statement, expectedText: 'SELECT * FROM something' })
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
      expected: statement,
    })
    testEnforceNoLimit({ statement, expectedText: 'SELECT * FROM something' })
  })

  it('Limit offset, row_count syntax', () => {
    const statement = {
      text: 'SELECT * FROM something limit 5, 1000',
      limit: 1000,
      offset: 5,
      type: 'select',
    }
    testInjectLimitOffset({ sql: `SELECT * FROM something limit 5, 10000`, expected: statement })
    testEnforceNoLimit({ statement, expectedText: 'SELECT * FROM something' })
  })

  it('Multi statements', () => {
    testInjectLimitOffset({
      sql: `SELECT * FROM t1; SELECT * FROM t2; /* This is a comment */`,
      multi: true,
      expected: [
        {
          text: 'SELECT * FROM t1 limit 1000 offset 10',
          limit: 1000,
          offset: 10,
          type: 'select',
        },
        {
          text: 'SELECT * FROM t2 limit 1000 offset 10',
          limit: 1000,
          offset: 10,
          type: 'select',
        },
        { text: '/* This is a comment */', limit: undefined, offset: undefined, type: undefined },
      ],
    })
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
      expected: statement,
    })
    testEnforceNoLimit({
      statement,
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
      expected: {
        text: 'SELECT * FROM something limit 1000 offset 10',
        limit: 1000,
        offset: 10,
        type: 'select',
      },
    })
    testEnforceNoLimit({ statement, expectedText: 'SELECT * FROM something' })
  })

  it('Handles existing offset with enforce mode', () => {
    testInjectLimitOffset({
      sql: 'SELECT * FROM something limit 1000 offset 0',
      shouldReplace: true,
      expected: {
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
      expected: {
        text: 'SELECT * FROM something limit 10, 1000',
        limit: 1000,
        offset: 10,
        type: 'select',
      },
    })
  })
})
