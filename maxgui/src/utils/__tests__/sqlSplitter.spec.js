/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import sqlSplitter from '@/utils/sqlSplitter'

describe('sqlSplitter', () => {
  it('Should correctly split 2 queries', () => {
    const [, statements] = sqlSplitter('SELECT * FROM `t1`;SELECT * FROM `t2`;')
    expect(statements.map((stmt) => stmt.text)).toStrictEqual([
      'SELECT * FROM `t1`',
      'SELECT * FROM `t2`',
    ])
  })

  it('Should handle delimiter change', () => {
    const [, statements] = sqlSplitter(
      'SELECT 1;\n DELIMITER $$\n SELECT 2; SELECT 3$$\n DELIMITER ;\n SELECT 4'
    )
    expect(statements.map((stmt) => stmt.text)).toEqual([
      'SELECT 1',
      'SELECT 2; SELECT 3',
      'SELECT 4',
    ])
    expect(statements.map((stmt) => stmt.delimiter)).toEqual([';', '$$', ';'])
  })
})
