/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { t } from 'typy'
import { lodash, uuidv1, unquoteIdentifier } from '@share/utils/helpers'
import tokenizer from '@wsSrc/utils/createTableTokenizer'
import { CREATE_TBL_TOKENS as tokens, REF_OPTS } from '@wsSrc/constants'

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
            const key = match[1].toLocaleLowerCase()
            const value = match[2]
            opts[key] = key === 'comment' ? unquoteIdentifier(value) : value
        }
        return opts
    }
    /**
     * Parse create and column defs
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
                generated,
                ai,
                default_exp,
                comment,
            } = match.groups
            let parsedDef = {
                name: unquoteIdentifier(name),
                data_type: `${data_type.toUpperCase()}${
                    data_type_size ? `(${data_type_size})` : ''
                }`,
                un: Boolean(un),
                zf: Boolean(zf),
                nn: Boolean(nn),
                charset:
                    this.skipColCharsetIfEqual && charset === this.tblCharset ? undefined : charset,
                collate,
                generated,
                ai: Boolean(ai),
                default_exp: default_exp || generated_exp,
                comment: unquoteIdentifier(comment),
            }
            if (this.autoGenId) parsedDef.id = `col_${uuidv1()}`
            return parsedDef
        }
        return def
    }
    /**
     * @param {string} col_names .e.g. `last_name`(30) ASC,`first_name`
     * @returns {object[]}
     */
    parseKeyColNames(col_names) {
        if (!col_names) return undefined
        let match
        let res = []
        while ((match = indexColNamesReg.exec(col_names)) !== null) {
            const { name, length, order } = match.groups
            res.push(
                lodash.pickBy(
                    { name: unquoteIdentifier(name), length, order },
                    v => v !== undefined
                )
            )
        }
        return res
    }
    /**
     * Parses a string to extract its key
     * @param {String} def - A string containing key definition.
     * @returns {object}
     */
    parseKey(def) {
        const match = def.match(nonFksReg) || def.match(fksReg)
        if (!match) return null
        const {
            category,
            name,
            comment,
            col_names,
            ref_col_names,
            ref_schema_name,
            ref_tbl_name,
            on_delete = REF_OPTS.NO_ACTION,
            on_update = REF_OPTS.NO_ACTION,
        } = match.groups

        let parsed = {
            cols: this.parseKeyColNames(col_names),
        }
        if (this.autoGenId) parsed.id = `key_${uuidv1()}`
        if (comment) parsed.comment = unquoteIdentifier(comment)
        if (category !== tokens.primaryKey) parsed.name = unquoteIdentifier(name)
        if (category === tokens.foreignKey)
            parsed = {
                ...parsed,
                ref_cols: this.parseKeyColNames(ref_col_names),
                /**
                 * If ref_schema_name is not defined, the referenced table is in the
                 * same schema as the table being parsed.
                 */
                ref_schema_name: ref_schema_name ? unquoteIdentifier(ref_schema_name) : this.schema,
                ref_tbl_name: unquoteIdentifier(ref_tbl_name),
                on_delete,
                on_update,
            }
        return { value: parsed, category }
    }
    /**
     * @param {String} defsStr - table defs including create, column and constraint defs
     * @returns {Object}
     */
    parseTableDefs(defsStr) {
        const defLines = defsStr.split('\n')
        let col_map = {},
            key_category_map = {}
        defLines.forEach(def => {
            const parsedDef = this.parseColDef(def.trim().replace(/,\s*$/, ''))
            if (parsedDef) {
                if (t(parsedDef).isString) {
                    const { category, value } = this.parseKey(parsedDef) || {}
                    if (category) {
                        if (!key_category_map[category]) key_category_map[category] = {}
                        key_category_map[category][value.id] = value
                    }
                } else if (parsedDef.id) {
                    col_map[parsedDef.id] = parsedDef
                }
            }
        })
        return { col_map, key_category_map }
    }
    /** Parse the result of SHOW CREATE TABLE
     * @param {object} param
     * @param {string} param.ddl - result of SHOW CREATE TABLE
     * @param {string} param.schema - name of the schema
     * @param {boolean} [param.autoGenId] - if true, id will be generated for the table and its columns
     * @param {boolean} [param.skipColCharsetIfEqual] - if true, when column charset is equal to
     * table charset, it will be parsed to undefined.
     * @returns {object} parsed ddl
     */
    parse({ ddl, schema, autoGenId = false, skipColCharsetIfEqual = true }) {
        this.autoGenId = autoGenId
        this.schema = schema
        this.skipColCharsetIfEqual = skipColCharsetIfEqual
        const match = ddl.match(createTableReg)
        let defs, options
        if (match) {
            const { table_name, table_options, table_definitions } = match.groups
            options = this.parseTableOpts(table_options)
            options.schema = schema
            options.name = unquoteIdentifier(table_name)
            this.tblCharset = options.charset
            defs = this.parseTableDefs(table_definitions)
        }
        let parsed = { defs, options }
        if (this.autoGenId) parsed.id = `tbl_${uuidv1()}`
        return parsed
    }
}
