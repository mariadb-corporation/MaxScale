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
})
