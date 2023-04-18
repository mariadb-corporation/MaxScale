/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import TableParser from '@wsSrc/utils/TableParser'

describe('TableParser', () => {
    let parser
    const sql = `
    CREATE TABLE 'my_table' (
        id INT AUTO_INCREMENT PRIMARY KEY, name VARCHAR(255) NOT NULL
     ) ENGINE = InnoDB AUTO_INCREMENT = 100 DEFAULT CHARSET = utf8mb4
       COLLATE = utf8mb4_general_ci COMMENT = "My table"
    `

    const expectTableOpts = {
        engine: 'InnoDB',
        auto_increment: '100',
        charset: 'utf8mb4',
        collate: 'utf8mb4_general_ci',
        comment: 'My table',
    }
    beforeEach(() => {
        parser = new TableParser()
    })
    describe('parseTableOpts', () => {
        it('should parse the table options string into an object', () => {
            expect(parser.parseTableOpts(sql)).to.be.eql(expectTableOpts)
        })
    })
    describe('parse', () => {
        it('should parse a CREATE TABLE statement', function() {
            expect(parser.parse(sql)).to.be.eql({
                tbl_name: 'my_table',
                table_options: expectTableOpts,
                table_definitions: 'id INT AUTO_INCREMENT PRIMARY KEY, name VARCHAR(255) NOT NULL',
            })
        })
    })
})
