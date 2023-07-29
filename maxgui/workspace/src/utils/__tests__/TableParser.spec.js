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
import { lodash } from '@share/utils/helpers'
import { unquoteIdentifier } from '@wsSrc/utils/helpers'
import TableParser from '@wsSrc/utils/TableParser'
import { CREATE_TBL_TOKENS as tokens, REF_OPTS } from '@wsSrc/store/config'

function stubColDef({
    name,
    data_type,
    data_type_size,
    is_un = false,
    is_zf = false,
    is_nn = false,
    charset,
    collate,
    generated_exp,
    generated_type,
    is_ai = false,
    default_exp,
    comment,
}) {
    return {
        name,
        data_type,
        data_type_size,
        is_un,
        is_zf,
        is_nn,
        charset,
        collate,
        generated_exp,
        generated_type,
        is_ai,
        default_exp,
        comment,
    }
}

function stubKeyDef({
    category,
    name,
    cols,
    ref_cols,
    ref_schema_name,
    ref_tbl_name,
    on_delete = REF_OPTS.NO_ACTION,
    on_update = REF_OPTS.NO_ACTION,
}) {
    let mockParsedData = { cols }
    if (category !== tokens.primaryKey) mockParsedData.name = name
    if (category === tokens.foreignKey)
        mockParsedData = {
            ...mockParsedData,
            ref_cols,
            ref_schema_name,
            ref_tbl_name,
            on_delete,
            on_update,
        }
    return mockParsedData
}

function stubIndexColNameDef(def) {
    return lodash.pickBy(def, v => v !== undefined)
}

const expectedColDefs = {
    '`id` INT unsigned AUTO_INCREMENT': stubColDef({
        name: 'id',
        data_type: 'INT',
        is_un: true,
        is_ai: true,
    }),
    '`name` VARCHAR(255) NOT NULL': stubColDef({
        name: 'name',
        data_type: 'VARCHAR',
        data_type_size: '255',
        is_nn: true,
    }),
    '`my ``s `` col` VARCHAR(30) CHARACTER SET big5 COLLATE big5_bin  DEFAULT "\'`"" "': stubColDef(
        {
            name: 'my `s ` col',
            data_type: 'VARCHAR',
            data_type_size: '30',
            charset: 'big5',
            collate: 'big5_bin',
            default_exp: '"\'`"" "',
        }
    ),
    '`col_timestamp` timestamp NOT NULL DEFAULT current_timestamp()': stubColDef({
        name: 'col_timestamp',
        data_type: 'timestamp',
        is_nn: true,
        default_exp: 'current_timestamp()',
    }),
    '`vtxt` varchar(5) GENERATED ALWAYS AS (rtrim(`txt`)) STORED': stubColDef({
        name: 'vtxt',
        data_type: 'varchar',
        data_type_size: '5',
        generated_exp: 'rtrim(`txt`)',
        generated_type: 'STORED',
    }),
    '`negative_int` int(11) DEFAULT -11': stubColDef({
        name: 'negative_int',
        data_type: 'int',
        data_type_size: '11',
        default_exp: '-11',
    }),
    "`col_comment` varchar(255) DEFAULT NULL COMMENT '`col``s comment`'": stubColDef({
        name: 'col_comment',
        data_type: 'varchar',
        data_type_size: '255',
        default_exp: 'NULL',
        comment: '`col``s comment`',
    }),
    '`zerofill` INT zerofill': stubColDef({
        name: 'zerofill',
        data_type: 'INT',
        is_zf: true,
    }),
    '`a` int DEFAULT (1+1) COMMENT "(1+1)"': stubColDef({
        name: 'a',
        data_type: 'int',
        default_exp: '(1+1)',
        comment: '(1+1)',
    }),
}

const expectedColIndexNameDefs = {
    '`col_int`': [stubIndexColNameDef({ name: 'col_int' })],
    '`col_invisible`,`col_string`': [
        stubIndexColNameDef({ name: 'col_invisible' }),
        stubIndexColNameDef({ name: 'col_string' }),
    ],
    '`last_name`(30) DESC,`first_name`(30)': [
        stubIndexColNameDef({ name: 'last_name', length: '30', order: 'DESC' }),
        stubIndexColNameDef({ name: 'first_name', length: '30' }),
    ],
    '`last_name`(30) ASC,`first_name`': [
        stubIndexColNameDef({ name: 'last_name', length: '30', order: 'ASC' }),
        stubIndexColNameDef({ name: 'first_name' }),
    ],
}

const mockKeyStr = [
    'PRIMARY KEY (`col_int`)',
    'UNIQUE KEY `col_invisible_UNIQUE` (`col_invisible`)',
    'UNIQUE KEY `col_invisible_col_string_UNIQUE` (`col_invisible`,`col_string`)',
    'KEY `col_a_PLAIN` (`col_a`)',
    'KEY `name_idx` (`last_name`(30) DESC,`first_name`(30))',
    'CONSTRAINT `songs_album_id` FOREIGN KEY (`album_id`) REFERENCES `test`.`albums` (`id`)',
    "CONSTRAINT `orders_ibfk_1` FOREIGN KEY (`customer's id`) REFERENCES " +
        '`test`.`customers` (`id`) ON DELETE CASCADE',
    "CONSTRAINT `orders_ibfk_2` FOREIGN KEY (`customer's id`) REFERENCES " +
        '`test`.`customers` (`id`) ON UPDATE SET NULL',
    "CONSTRAINT `orders_ibfk_3` FOREIGN KEY (`customer's id`) REFERENCES " +
        '`test`.`customers` (`id`) ON DELETE CASCADE ON UPDATE NO ACTION',
    'CONSTRAINT `orders_ibfk_4` FOREIGN KEY (`customer_id`) REFERENCES ' +
        '`test`.`customers` (`customer_id`)',
]
const expectedParsedKeys = {
    [tokens.primaryKey]: [
        stubKeyDef({
            category: tokens.primaryKey,
            cols: [stubIndexColNameDef({ name: 'col_int' })],
        }),
    ],
    [tokens.uniqueKey]: [
        stubKeyDef({
            category: tokens.uniqueKey,
            name: 'col_invisible_UNIQUE',
            cols: [stubIndexColNameDef({ name: 'col_invisible' })],
        }),
        stubKeyDef({
            category: tokens.uniqueKey,
            name: 'col_invisible_col_string_UNIQUE',
            cols: [
                stubIndexColNameDef({ name: 'col_invisible' }),
                stubIndexColNameDef({ name: 'col_string' }),
            ],
        }),
    ],
    [tokens.key]: [
        stubKeyDef({
            category: tokens.key,
            name: 'col_a_PLAIN',
            cols: [stubIndexColNameDef({ name: 'col_a' })],
        }),
        stubKeyDef({
            category: tokens.key,
            name: 'name_idx',
            cols: [
                stubIndexColNameDef({ name: 'last_name', length: '30', order: 'DESC' }),
                stubIndexColNameDef({ name: 'first_name', length: '30' }),
            ],
        }),
    ],
    [tokens.foreignKey]: [
        stubKeyDef({
            category: tokens.foreignKey,
            name: 'songs_album_id',
            cols: [stubIndexColNameDef({ name: 'album_id' })],
            ref_cols: [stubIndexColNameDef({ name: 'id' })],
            ref_schema_name: 'test',
            ref_tbl_name: 'albums',
        }),
        stubKeyDef({
            category: tokens.foreignKey,
            name: 'orders_ibfk_1',
            cols: [stubIndexColNameDef({ name: "customer's id" })],
            ref_cols: [stubIndexColNameDef({ name: 'id' })],
            ref_schema_name: 'test',
            ref_tbl_name: 'customers',
            on_delete: 'CASCADE',
        }),
        stubKeyDef({
            category: tokens.foreignKey,
            name: 'orders_ibfk_2',
            cols: [stubIndexColNameDef({ name: "customer's id" })],
            ref_cols: [stubIndexColNameDef({ name: 'id' })],
            ref_schema_name: 'test',
            ref_tbl_name: 'customers',
            on_update: 'SET NULL',
        }),
        stubKeyDef({
            category: tokens.foreignKey,
            name: 'orders_ibfk_3',
            cols: [stubIndexColNameDef({ name: "customer's id" })],
            ref_cols: [stubIndexColNameDef({ name: 'id' })],
            ref_schema_name: 'test',
            ref_tbl_name: 'customers',
            on_delete: 'CASCADE',
            on_update: 'NO ACTION',
        }),
        stubKeyDef({
            category: tokens.foreignKey,
            name: 'orders_ibfk_4',
            cols: [stubIndexColNameDef({ name: 'customer_id' })],
            ref_cols: [stubIndexColNameDef({ name: 'customer_id' })],
            ref_schema_name: 'test',
            ref_tbl_name: 'customers',
        }),
    ],
}

const tableOpt = `ENGINE=InnoDB AUTO_INCREMENT=100 DEFAULT CHARSET=utf8mb4 COMMENT="My table"`
const expectTableOpts = {
    engine: 'InnoDB',
    auto_increment: '100',
    charset: 'utf8mb4',
    comment: 'My table',
}

const tableDefStr = [...Object.keys(expectedColDefs), ...mockKeyStr].join(',\n')
const tblNames = ['`my_table`', '`my test``s  table`', '`test\'s table "`', '`table with ""`']
const tables = tblNames.map(name => `CREATE TABLE ${name} (\n${tableDefStr}\n) ${tableOpt}`)

const expectedTableDefs = {
    cols: Object.values(expectedColDefs),
    keys: expectedParsedKeys,
}
describe('TableParser', () => {
    let parser

    beforeEach(() => {
        parser = new TableParser()
    })
    describe('parseTableOpts', () => {
        it('should parse the table options string', () => {
            expect(parser.parseTableOpts(tableOpt)).to.be.eql(expectTableOpts)
        })
    })
    describe('parseColDef', () => {
        Object.keys(expectedColDefs).forEach(defStr => {
            it(`Should parse: ${defStr}`, () => {
                expect(parser.parseColDef(defStr)).to.be.eql(expectedColDefs[defStr])
            })
        })
    })
    describe('parseKeyColNames', () => {
        Object.keys(expectedColIndexNameDefs).forEach(str => {
            it(`Should parse: ${str}`, () => {
                expect(parser.parseKeyColNames(str)).to.be.eql(expectedColIndexNameDefs[str])
            })
        })
    })
    describe('parseKey', () => {
        const allKeys = Object.values(expectedParsedKeys).flat()
        mockKeyStr.forEach((str, i) => {
            it(`Should parse: ${str}`, () => {
                const { value } = parser.parseKey(str) || {}
                expect(value).to.be.eql(allKeys[i])
            })
        })
    })
    describe('parseTableDefs', () => {
        it(`Should parse table definitions string part`, () => {
            expect(parser.parseTableDefs(tableDefStr)).to.be.eql(expectedTableDefs)
        })
    })
    describe('parse', () => {
        tables.forEach((ddl, i) => {
            it('should parse a CREATE TABLE statement', () => {
                expect(parser.parse({ ddl, schema: 'test' })).to.be.eql({
                    definitions: expectedTableDefs,
                    options: {
                        ...expectTableOpts,
                        schema: 'test',
                        name: unquoteIdentifier(tblNames[i]),
                    },
                })
            })
        })
    })
})
