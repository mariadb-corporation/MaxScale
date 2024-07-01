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

function testInjectLimitOffset(sql, expected) {
  expect(queryUtils.injectLimitOffset({ sql, limitNumber: 1000, offsetNumber: 10 })).toStrictEqual(
    expected
  )
}

describe('queryUtils', () => {
  describe('injectLimitOffset', () => {
    it('Limit and offset are not defined', () => {
      testInjectLimitOffset('SELECT * FROM something', [
        { text: 'SELECT * FROM something limit 1000 offset 10', limit: 1000, offset: 10 },
      ])
    })

    it('Both limit and offset are defined', () => {
      testInjectLimitOffset('SELECT * FROM something limit 10000 offset 1', [
        { text: 'SELECT * FROM something limit 1000 offset 1', limit: 1000, offset: 1 },
      ])
    })

    it('Limit offset, row_count syntax', () => {
      testInjectLimitOffset(`SELECT * FROM something limit 5, 10000`, [
        { text: 'SELECT * FROM something limit 5, 1000', limit: 1000, offset: 5 },
      ])
    })

    it('Multi statements', () => {
      testInjectLimitOffset(`SELECT * FROM t1; SELECT * FROM t2; /* This is a comment */`, [
        { text: 'SELECT * FROM t1 limit 1000 offset 10', limit: 1000, offset: 10 },
        { text: 'SELECT * FROM t2 limit 1000 offset 10', limit: 1000, offset: 10 },
        { text: '/* This is a comment */', limit: undefined, offset: undefined },
      ])
    })

    it('handles subquery', function () {
      testInjectLimitOffset(`SELECT * FROM ( select something OFFSET 1 ROW )`, [
        {
          text: 'SELECT * FROM ( select something OFFSET 1 ROW ) limit 1000 offset 10',
          limit: 1000,
          offset: 10,
        },
      ])
    })
  })
})
