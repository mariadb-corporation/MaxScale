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
import { t } from 'typy'
import tokenizer from '@wsSrc/utils/createTableTokenizer'

const tableOptionsReg = tokenizer.tableOptions
const colDefReg = tokenizer.colDef
const nonFksReg = tokenizer.nonFks
const fksReg = tokenizer.fks
const indexColNamesReg = tokenizer.indexColNames
const createTableReg = tokenizer.createTable
/**
 * This parser works when sql_quote_show_create on
 */
export default class TableParser {
    /**
     * @param {String} optsStr - table options string
     * @returns {Object} - table options
     */
    parseTableOpts(optsStr) {
        let match
        let opts = {}
        while ((match = tableOptionsReg.exec(optsStr)) !== null) {
            const key = match[1]
            const value = match[2]
            opts[key.toLocaleLowerCase()] = value
        }
        return opts
    }
    /**
     * Parse create and column definitions
     * @param {String} def - Column definition string
     * @returns {object|string} - object if it is column def, otherwise initial def string will be returned
     */
    parseColDef(def) {
        const match = def.match(colDefReg)
        if (match) {
            const {
                col_name: name,
                data_type,
                data_type_size,
                un,
                zf,
                nn,
                charset,
                collate,
                generated_exp,
                generated_type,
                ai,
                default_exp,
                comment,
            } = match.groups
            return {
                name,
                data_type,
                data_type_size,
                is_un: Boolean(un),
                is_zf: Boolean(zf),
                is_nn: Boolean(nn),
                charset,
                collate,
                generated_exp,
                generated_type,
                is_ai: Boolean(ai),
                default_exp,
                comment,
            }
        }
        return def
    }
    /**
     * @param {string} index_col_names .e.g. `last_name`(30) ASC,`first_name`
     * @returns {object[]}
     */
    parseIndexColNames(index_col_names) {
        if (!index_col_names) return undefined
        let match
        let res = []
        while ((match = indexColNamesReg.exec(index_col_names)) !== null) {
            const { name, length, order } = match.groups
            res.push({ name, length, order })
        }
        return res
    }
    /**
     * Parses a string to extract its key
     * @param {String} def - A string containing key definition.
     * @returns {object|string}
     */
    parseKey(def) {
        const match = def.match(nonFksReg) || def.match(fksReg)
        if (!match) {
            return def
        }
        const {
            category,
            name,
            index_col_names,
            match_option,
            referenced_index_col_names,
            referenced_schema_name,
            referenced_table_name,
            on_delete,
            on_update,
        } = match.groups
        return {
            category,
            name,
            index_cols: this.parseIndexColNames(index_col_names),
            match_option,
            referenced_index_cols: this.parseIndexColNames(referenced_index_col_names),
            referenced_schema_name,
            referenced_table_name,
            on_delete,
            on_update,
        }
    }
    /**
     * @param {String} defsStr - table definitions including create, column and constraint definitions
     * @returns {Object}
     */
    parseTableDefs(defsStr) {
        const defLines = defsStr.split('\n')
        let cols = [],
            keys = {}
        defLines.forEach(def => {
            const parsedDef = this.parseColDef(def.trim().replace(/,\s*$/, ''))
            if (parsedDef) {
                if (t(parsedDef).isString) {
                    const parsedKey = this.parseKey(parsedDef)
                    const category = parsedKey.category
                    if (!keys[category]) keys[category] = []
                    keys[category].push(parsedKey)
                } else cols.push(parsedDef)
            }
        })
        return { cols, keys }
    }
    // Parse the result of SHOW CREATE TABLE
    parse(sql) {
        const match = sql.match(createTableReg)
        let name, definitions, options
        if (match) {
            const { table_name, table_options, table_definitions } = match.groups
            name = table_name
            definitions = this.parseTableDefs(table_definitions)
            options = this.parseTableOpts(table_options)
        }
        return {
            name,
            definitions,
            options,
        }
    }
}
