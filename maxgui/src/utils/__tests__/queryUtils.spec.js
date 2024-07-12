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
import * as queryUtils from '@/utils/queryUtils'

function testInjectLimitOffset({ sql, multi, shouldReplace, expected }) {
  expect(
    queryUtils.injectLimitOffset({
      sql,
      multi,
      shouldReplace,
      limitNumber: 1000,
      offsetNumber: 10,
    })
  ).toStrictEqual(expected)
}

function testMaskQueryPwd(sql, expected) {
  expect(queryUtils.maskQueryPwd(sql)).toBe(expected)
}

describe('queryUtils', () => {
  describe('maskQueryPwd', () => {
    it('Masks password in IDENTIFIED BY pattern', () => {
      testMaskQueryPwd(
        "CREATE USER 'username' IDENTIFIED BY 'password';",
        "CREATE USER 'username' IDENTIFIED BY '***';"
      )
    })

    it('Masks password in PLUGIN PWD pattern', () => {
      testMaskQueryPwd(
        "CREATE USER 'username' IDENTIFIED BY PASSWORD('password');",
        "CREATE USER 'username' IDENTIFIED BY PASSWORD('***');"
      )
    })

    it('Masks password in IDENTIFIED VIA/WITH pattern', () => {
      testMaskQueryPwd(
        "CREATE USER 'username' IDENTIFIED VIA plugin USING 'password';",
        "CREATE USER 'username' IDENTIFIED VIA plugin USING '***';"
      )
    })

    it('Handles nested PASSWORD() function', () => {
      testMaskQueryPwd(
        "CREATE USER 'username' IDENTIFIED BY PASSWORD(PASSWORD('password'));",
        "CREATE USER 'username' IDENTIFIED BY PASSWORD(PASSWORD('***'));"
      )
    })

    it('Masks passwords with special characters', () => {
      testMaskQueryPwd(
        "CREATE USER 'username' IDENTIFIED BY 'pass@word!';",
        "CREATE USER 'username' IDENTIFIED BY '***';"
      )
    })

    it('Returns original query if no identifiable patterns are found', () => {
      testMaskQueryPwd("CREATE USER 'username';", "CREATE USER 'username';")
    })
  })

  describe('stringifyErrResult', () => {
    it('transforms basic object into formatted string', () => {
      expect(
        queryUtils.stringifyErrResult({
          errno: 1064,
          message: 'You have an error in your SQL syntax',
          sqlstate: 42000,
        })
      ).toBe('Errno: 1064. Message: You have an error in your SQL syntax. Sqlstate: 42000.')
    })
  })
  describe('injectLimitOffset', () => {
    it('Limit and offset are not defined', () => {
      testInjectLimitOffset({
        sql: 'SELECT * FROM something',
        expected: {
          text: 'SELECT * FROM something limit 1000 offset 10',
          limit: 1000,
          offset: 10,
          type: 'select',
        },
      })
    })

    it('Both limit and offset are defined', () => {
      testInjectLimitOffset({
        sql: 'SELECT * FROM something limit 10000 offset 1',
        expected: {
          text: 'SELECT * FROM something limit 1000 offset 1',
          limit: 1000,
          offset: 1,
          type: 'select',
        },
      })
    })

    it('Limit offset, row_count syntax', () => {
      testInjectLimitOffset({
        sql: `SELECT * FROM something limit 5, 10000`,
        expected: {
          text: 'SELECT * FROM something limit 5, 1000',
          limit: 1000,
          offset: 5,
          type: 'select',
        },
      })
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
      testInjectLimitOffset({
        sql: `SELECT * FROM ( select something OFFSET 1 ROW )`,
        expected: {
          text: 'SELECT * FROM ( select something OFFSET 1 ROW ) limit 1000 offset 10',
          limit: 1000,
          offset: 10,
          type: 'select',
        },
      })
    })

    it('Handles line-break', () => {
      testInjectLimitOffset({
        sql: 'SELECT * FROM something;\n',
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
})
