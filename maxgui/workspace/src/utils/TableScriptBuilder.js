/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { t as typy } from 'typy'
import {
    lodash,
    deepDiff,
    arrOfObjsDiff,
    quotingIdentifier as quoting,
    addComma,
    escapeSingleQuote,
} from '@share/utils/helpers'
import { formatSQL } from '@wsSrc/utils/queryUtils'
import erdHelper from '@wsSrc/utils/erdHelper'
import { CREATE_TBL_TOKENS as tokens, ALL_TABLE_KEY_CATEGORIES } from '@wsSrc/constants'

/**
 * Table script builder.
 * This is designed to work with the output of erdHelper.genDdlEditorData,
 * which is a data structure representing the parsed information of a table.
 * @typedef {Object} TableScriptBuilder
 */
export default class TableScriptBuilder {
    /**
     * @constructor
     * @param {object}
     * @param {object} param.initialData
     * @param {object} param.stagingData
     * @param {object} param.refTargetMap - reference target. e.g { tbl_id: { id, text }}. text is table qualified name
     * @param {Object.<string, Object.<string, string>>} param.tablesColNameMap { "tbl_id": { "col_id": "id",... } }
     * @param {boolean} param.options.isCreating - if true, this class outputs CREATE TABLE script
     * @param {boolean} param.options.skipSchemaCreation - if true, this class won't output schema creation for new
     * table
     * @param {boolean} param.options.skipFkCreation - if true, build method will skip FKs creation
     */
    constructor({
        initialData,
        stagingData,
        refTargetMap,
        tablesColNameMap,
        options: { isCreating = false, skipSchemaCreation = false, skipFkCreation = false } = {},
    }) {
        // initialData is an empty object if `isCreating` is true
        this.initialData = initialData
        this.initialSchemaName = typy(initialData, 'options.schema').safeString
        this.initialTableName = typy(initialData, 'options.name').safeString
        this.initialCols = Object.values(typy(initialData, 'defs.col_map').safeObjectOrEmpty)

        this.stagingSchemaName = typy(stagingData, 'options.schema').safeString
        this.stagingTableName = typy(stagingData, 'options.name').safeString
        this.stagingDataOptions = stagingData.options

        this.stagingCols = Object.values(typy(stagingData, 'defs.col_map').safeObjectOrEmpty)

        this.optionDiffs = deepDiff(
            typy(initialData, 'options').safeObjectOrEmpty,
            this.stagingDataOptions
        )
        this.hasColDefChanged = !lodash.isEqual(this.initialCols, this.stagingCols)

        // Keys
        const initialKeyCategoryMap = typy(initialData, 'defs.key_category_map').safeObjectOrEmpty
        const stagingKeyCategoryMap = typy(stagingData, 'defs.key_category_map').safeObjectOrEmpty

        this.keyDiffs = ALL_TABLE_KEY_CATEGORIES.reduce((map, category) => {
            map[category] = this.getKeysDiffs({
                category,
                initialKeyCategoryMap,
                stagingKeyCategoryMap,
            })
            return map
        }, {})

        this.refTargetMap = refTargetMap
        this.tablesColNameMap = tablesColNameMap
        this.stagingColNameMap = typy(tablesColNameMap, `[${stagingData.id}]`).safeObjectOrEmpty

        // options
        this.isCreating = isCreating
        this.skipSchemaCreation = skipSchemaCreation
        this.skipFkCreation = skipFkCreation
    }

    /**
     * @param {object} param
     * @param {object} param.keyCategoryMap - key map
     * @param {string}  param.category - key category
     * @returns {Array.<object} keyCategoryMap
     */
    getKeys({ keyCategoryMap, category }) {
        return Object.values(keyCategoryMap[category] || {})
    }

    /**
     * @param {object} param
     * @param {object} param.initialKeyCategoryMap - initial keyCategoryMap
     * @param {object} param.stagingKeyCategoryMap - staging keyCategoryMap
     * @param {string} param.category - key category
     * @returns {object}
     */
    getKeysDiffs({ category, initialKeyCategoryMap, stagingKeyCategoryMap }) {
        const initial = this.getKeys({ keyCategoryMap: initialKeyCategoryMap, category })
        const staging = this.getKeys({ keyCategoryMap: stagingKeyCategoryMap, category })
        return arrOfObjsDiff({ base: initial, newArr: staging, idField: 'id' })
    }

    /**
     * This builds table_options SQL
     * @returns {String} - returns table_options sql
     */
    buildTblOptsSql() {
        let parts = []
        this.optionDiffs.forEach(diff => {
            const key = diff.path[0]
            switch (key) {
                case 'name':
                    if (this.isCreating) break
                    parts.push(`RENAME TO ${quoting(this.initialSchemaName)}.${quoting(diff.rhs)}`)
                    break
                case 'engine':
                    parts.push(`ENGINE = ${diff.rhs}`)
                    break
                case 'charset':
                    parts.push(`CHARACTER SET = ${diff.rhs}`)
                    break
                case 'collation':
                    parts.push(`COLLATE = ${diff.rhs}`)
                    break
                case 'comment':
                    parts.push(`COMMENT = '${escapeSingleQuote(diff.rhs)}'`)
                    break
            }
        })
        if (this.isCreating) return parts.join(' ')
        return parts.join(addComma())
    }

    /**
     * This builds column definition SQL.
     * If altering an existing table, it returns CHANGE/ADD COLUMN definition.
     * Otherwise, it returns just column definition.
     * @param {object} payload.col
     * @param {boolean} payload.isUpdated
     * @returns {String} - returns column definition SQL
     */
    buildColDfnSQL({ col, isUpdated }) {
        let sql = ''
        const colObj = isUpdated ? typy(col, 'newObj').safeObjectOrEmpty : col
        const {
            name,
            data_type,
            nn,
            un,
            zf,
            ai,
            generated,
            default_exp,
            charset,
            collate,
            comment,
        } = colObj
        if (!this.isCreating)
            if (isUpdated) {
                const oldColName = typy(col, `oriObj.name`).safeString
                sql += `${tokens.change} ${tokens.column} ${quoting(oldColName)} `
            } else sql += `${tokens.add} ${tokens.column} `

        sql += `${quoting(name)}`
        sql += ` ${data_type}`
        if (un && data_type !== 'SERIAL') sql += ` ${tokens.un}`
        if (zf) sql += ` ${tokens.zf}`
        if (charset) sql += ` ${tokens.charset} ${charset}`
        if (collate) sql += ` ${tokens.collate} ${collate}`
        // when column is generated, NN or NULL can not be defined
        if (!generated && data_type !== 'SERIAL') sql += ` ${nn ? tokens.nn : tokens.null}`
        if (ai && data_type !== 'SERIAL') sql += ` ${tokens.ai}`
        if (!generated && default_exp) sql += ` ${tokens.default} ${default_exp}`
        else if (generated) sql += ` ${tokens.generated} (${default_exp}) ${generated}`
        if (comment) sql += ` ${tokens.comment} '${escapeSingleQuote(comment)}'`
        return sql
    }

    /* This builds DROP column SQL
     * @param {Array} cols - columns need to be removed
     * @returns {String} - returns DROP COLUMN sql
     */
    buildRemovedColSQL(cols) {
        return cols.map(col => `DROP COLUMN ${quoting(col.name)}`).join(addComma())
    }

    buildColsSQL({ cols, isUpdated = false }) {
        return cols.map(col => this.buildColDfnSQL({ col, isUpdated })).join(addComma())
    }

    /**
     * @param {object} key - key object
     * @returns {string} e.g. `key_name (first_name`(50) DESC, `last_name`(20) ASC) COMMENT = 'comment'
     */
    genKeyDef({ name, cols, comment }) {
        let str = cols
            .map(({ id, order, length }) => {
                const name = quoting(this.stagingColNameMap[id])
                return `${name}${length ? `(${length})` : ''} ${order ? order : ''}`
            })
            .join(addComma())
        str = `(${str})`
        if (name) str = `${quoting(name)} ${str}`
        if (comment) str += ` ${tokens.comment} '${escapeSingleQuote(comment)}'`
        return str
    }

    /**
     * If altering an existing table, it returns DROP/ADD PK SQL.
     * Otherwise, it returns just PK definition.
     * @returns {String} - returns PK SQL
     */
    buildPkSQL() {
        const pkKeyDiffs = this.keyDiffs[tokens.primaryKey]
        const removedKey = typy(pkKeyDiffs.get('removed'), '[0]').safeObject
        const updatedKey = typy(pkKeyDiffs.get('updated'), '[0]').safeObject
        const addedKey = typy(pkKeyDiffs.get('added'), '[0]').safeObject
        const dropSQL = `${tokens.drop} ${tokens.primaryKey}`
        let parts = []
        if (removedKey) parts.push(dropSQL)
        if (addedKey || updatedKey) {
            if (updatedKey) parts.push(dropSQL)
            let keyDef = `${tokens.primaryKey} ${this.genKeyDef(
                updatedKey ? updatedKey.newObj : addedKey
            )}`
            if (!this.isCreating) keyDef = `${tokens.add} ${keyDef}`
            parts.push(keyDef)
        }
        return parts.join(addComma())
    }
    /**
     * @param {string} category - key category
     * @returns {string} - returns key SQL
     */
    buildKeySQL(category) {
        const keyDiffs = this.keyDiffs[category]
        const removedKeys = keyDiffs.get('removed')
        const updatedKeys = keyDiffs.get('updated')
        const addedKeys = keyDiffs.get('added')
        let parts = []
        parts = removedKeys.map(({ name }) => `${tokens.drop} ${tokens.key} ${quoting(name)}`)
        addedKeys.forEach(key => {
            const keyDef = `${category} ${this.genKeyDef(key)}`
            if (this.isCreating) parts.push(keyDef)
            else parts.push(`${tokens.add} ${keyDef}`)
        })
        updatedKeys.forEach(({ oriObj, newObj }) => {
            // Updating the key by firstly drop it and add then add a new one
            parts.push(`${tokens.drop} ${tokens.key} ${quoting(oriObj.name)}`)
            parts.push(`${tokens.add} ${category} ${this.genKeyDef(newObj)}`)
        })
        return parts.join(addComma())
    }
    buildOtherKeys() {
        let parts = []
        const categories = [tokens.uniqueKey, tokens.key, tokens.fullTextKey, tokens.spatialKey]
        categories.forEach(category => {
            const sql = this.buildKeySQL(category)
            if (sql) parts.push(sql)
        })
        return parts.join(addComma())
    }

    /**
     * This functions uses initial qualified name of a table, so
     * the SQL must be placed before any modification to qualified name.
     * @returns {string} sql for dropping FKs
     */
    buildRemovedFkSQL() {
        const foreignKeyDiffs = this.keyDiffs[tokens.foreignKey]
        /**
         * When altering existing keys, the keys needed to be dropped first
         * so that modified keys with existing name can be executed.
         * i.e. Duplicate key on write or update.
         */
        const removedKeys = [
            ...foreignKeyDiffs.get('removed'),
            ...foreignKeyDiffs.get('updated'),
        ].map(item => {
            const key = item.oriObj ? item.oriObj : item
            return `${tokens.drop} ${tokens.foreignKey} ${quoting(key.name)}`
        })
        let sql = ''
        const alterTableLine = `${tokens.alterTable} ${quoting(this.initialSchemaName)}.${quoting(
            this.initialTableName
        )}`
        if (removedKeys.length) {
            sql += alterTableLine
            sql += removedKeys.join(addComma())
            sql += ';'
        }
        return sql
    }
    /**
     * @param {object} param
     * @param {boolean} param.isPartOfTableCreation - if true, it returns `CONSTRAINT ...` instead
     * of `ADD CONSTRAINT ...`
     * @returns {String} - returns FK SQL
     */
    buildNewFkSQL({ isPartOfTableCreation = false } = {}) {
        const foreignKeyDiffs = this.keyDiffs[tokens.foreignKey]
        let sql = ''
        const alterTableLine = `${tokens.alterTable} ${quoting(this.stagingSchemaName)}.${quoting(
            this.stagingTableName
        )}`
        const updatedKeys = foreignKeyDiffs.get('updated')
        const addedKeys = foreignKeyDiffs.get('added')
        const keys = [...updatedKeys, ...addedKeys]
        const newFks = keys.map(item => {
            const constraintStr = erdHelper.genConstraint({
                // updatedKeys has `oriObj` and `newObj` fields while addedKeys doesn't
                key: item.newObj ? item.newObj : item,
                refTargetMap: this.refTargetMap,
                tablesColNameMap: this.tablesColNameMap,
                stagingColNameMap: this.stagingColNameMap,
            })
            if (isPartOfTableCreation) return constraintStr
            return `${tokens.add} ${constraintStr}`
        })

        if (newFks.length) {
            if (!isPartOfTableCreation) sql += alterTableLine
            sql += newFks.join(addComma())
            if (!isPartOfTableCreation) sql += ';'
        }
        return sql
    }

    /**
     * Build SQL for altering columns
     * @returns {String} - returns column alter sql
     */
    buildColDefAlterSQL() {
        const base = this.initialCols
        const newData = this.stagingCols
        const diff = arrOfObjsDiff({ base, newArr: newData, idField: 'id' })

        const removedCols = diff.get('removed')
        const updatedCols = diff.get('updated')
        const addedCols = diff.get('added')
        let alterSpecs = []
        // Build sql for different diff types
        const removedColSQL = this.buildRemovedColSQL(removedCols)
        const updatedColSQL = this.buildColsSQL({ cols: updatedCols, isUpdated: true })
        const addedColSQL = this.buildColsSQL({ cols: addedCols })

        if (removedColSQL) alterSpecs.push(removedColSQL)
        if (updatedColSQL) alterSpecs.push(updatedColSQL)
        if (addedColSQL) alterSpecs.push(addedColSQL)

        return alterSpecs.join(addComma())
    }

    /**
     * Build SQL for creating columns and keys
     * @returns {String} - returns column definition
     */
    buildCreateColsSql() {
        const data = this.stagingCols
        let specs = []
        const addedColSQL = this.buildColsSQL({ cols: data })
        specs.push(addedColSQL)
        const pkSQL = this.buildPkSQL()
        const otherKeysSQL = this.buildOtherKeys()
        const fkSQL = this.skipFkCreation ? '' : this.buildNewFkSQL({ isPartOfTableCreation: true })
        if (pkSQL) specs.push(pkSQL)
        if (otherKeysSQL) specs.push(otherKeysSQL)
        if (fkSQL) specs.push(fkSQL)
        return specs.join(addComma())
    }
    buildAlterScript() {
        let sql = ''
        /**
         * Removed FKs must be placed before any modifications to columns.
         * e.g. Dropping a column is a part for buildColDefAlterSQL, so the FK must be dropped first.
         */
        const removedFkSQL = this.buildRemovedFkSQL()
        if (removedFkSQL) sql += removedFkSQL

        let parts = [] // part of script which will be separated by commas
        if (this.optionDiffs && this.optionDiffs.length) parts.push(this.buildTblOptsSql())
        if (this.hasColDefChanged) {
            const colSql = this.buildColDefAlterSQL()
            if (colSql) parts.push(colSql)
        }

        const pkSQL = this.buildPkSQL()
        const otherKeysSQL = this.buildOtherKeys()
        if (pkSQL) parts.push(pkSQL)
        if (otherKeysSQL) parts.push(otherKeysSQL)

        if (parts.length) {
            sql += `${tokens.alterTable} ${quoting(this.initialSchemaName)}.${quoting(
                this.initialTableName
            )}`
            sql += parts.join(addComma())
            sql += ';'
        }

        const fkSQL = this.skipFkCreation ? '' : this.buildNewFkSQL()
        if (fkSQL) sql += fkSQL
        return formatSQL(sql)
    }

    buildCreateScript() {
        const { schema, name } = this.stagingDataOptions
        let sql = ''
        if (!this.skipSchemaCreation) sql += `CREATE SCHEMA IF NOT EXISTS ${quoting(schema)};`
        sql += `${tokens.createTable} IF NOT EXISTS ${quoting(schema)}.${quoting(name)} (`
        sql += `${this.buildCreateColsSql()})`
        sql += this.buildTblOptsSql()
        sql = formatSQL(`${sql};`)
        return sql
    }

    build() {
        if (this.isCreating) return this.buildCreateScript()
        return this.buildAlterScript()
    }
}
