/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import {
    formatSQL,
    quotingIdentifier as quoting,
    deepDiff,
    arrOfObjsDiff,
    map2dArr,
} from '@wsSrc/utils/helpers'
import { CREATE_TBL_TOKENS as tokens, COL_ATTRS, GENERATED_TYPES } from '@wsSrc/store/config'
import { lodash } from '@share/utils/helpers'
import { t as typy } from 'typy'

/**
 * Table script builder.
 * This is designed to work with the output of queryHelper.tableParserTransformer,
 * which is a data structure representing the parsed information of a table.
 */
export default class TableScriptBuilder {
    /**
     * @constructor
     * @param {object}
     * @param {object} param.initialData
     * @param {object} param.stagingData
     * @param {object} param.stagingColNameMap - hash map where column id is the key and value is the name
     * @param {boolean} param.isCreateTable - if true, this class outputs CREATE TABLE script
     */
    constructor({ initialData, stagingData, stagingColNameMap, isCreateTable }) {
        // initialData is an empty object if `isCreateTable` is true
        this.colAttrs = Object.values(COL_ATTRS)
        this.initialData = initialData
        this.initialSchemaName = typy(initialData, 'options.schema').safeString
        this.initialTableName = typy(initialData, 'options.name').safeString
        this.initialColsData = typy(initialData, 'definitions.cols').safeArray

        this.stagingDataOptions = stagingData.options
        this.stagingColNameMap = stagingColNameMap

        this.stagingColsData = typy(stagingData, 'definitions.cols').safeArray

        this.optionDiffs = deepDiff(
            typy(initialData, 'options').safeObjectOrEmpty,
            this.stagingDataOptions
        )
        this.isColsOptsChanged = !lodash.isEqual(this.initialColsData, this.stagingColsData)

        // Keys
        this.initialKeys = typy(initialData, 'definitions.keys').safeObjectOrEmpty
        this.stagingKeys = typy(stagingData, 'definitions.keys').safeObjectOrEmpty

        // Get the diff of pk, uq keys
        this.pkKeyDiffs = this.getKeysDiffs({
            initialKeys: this.initialKeys,
            stagingKeys: this.stagingKeys,
            category: tokens.primaryKey,
        })
        this.uqKeyDiffs = this.getKeysDiffs({
            initialKeys: this.initialKeys,
            stagingKeys: this.stagingKeys,
            category: tokens.uniqueKey,
        })

        // mode
        this.isCreateTable = isCreateTable
    }

    /**
     * @param {object} param
     * @param {object} param.keys - all keys
     * @param {string}  param.category - key category
     * @returns {Array.<object} keys
     */
    getKeys({ keys, category }) {
        return typy(keys, `${category}`).safeArray
    }

    /**
     * @param {object} param
     * @param {object} param.initialKeys - all initial keys
     * @param {object} param.stagingKeys - all staging keys
     * @param {string}  param.category - key category
     * @returns {object}
     */
    getKeysDiffs({ initialKeys, stagingKeys, category }) {
        const initial = this.getKeys({ keys: initialKeys, category })
        const staging = this.getKeys({ keys: stagingKeys, category })
        return arrOfObjsDiff({ base: initial, newArr: staging, idField: 'id' })
    }

    /**
     * @param {Boolean} payload.ignore - ignore adding comma
     * @returns {String} - return ', ' or ''
     */
    handleAddComma({ ignore = false } = {}) {
        return ignore ? '' : ', '
    }

    /**
     * This builds table_options SQL
     * @returns {String} - returns table_options sql
     */
    buildTblOptsSql() {
        let sql = ''
        this.optionDiffs.forEach((diff, i) => {
            if (this.isCreateTable) sql += ' '
            else sql += this.handleAddComma({ ignore: i === 0 })
            const key = diff.path[0]
            switch (key) {
                case 'name':
                    if (this.isCreateTable) break
                    sql += `RENAME TO ${quoting(this.initialSchemaName)}.${quoting(diff.rhs)}`
                    break
                case 'engine':
                    sql += `ENGINE = ${diff.rhs}`
                    break
                case 'charset':
                    sql += `CHARACTER SET = ${diff.rhs}`
                    break
                case 'collation':
                    sql += `COLLATE = ${diff.rhs}`
                    break
                case 'comment':
                    sql += `COMMENT = '${diff.rhs}'`
                    break
            }
        })
        return sql
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
        const {
            NAME,
            TYPE,
            NN,
            UN,
            ZF,
            AI,
            GENERATED_TYPE,
            DEF_EXP,
            CHARSET,
            COLLATE,
            COMMENT,
        } = COL_ATTRS
        let sql = ''
        const colObj = isUpdated ? typy(col, 'newObj').safeObjectOrEmpty : col

        const {
            [NAME]: name,
            [TYPE]: type,
            [NN]: nn,
            [UN]: un,
            [ZF]: zf,
            [AI]: ai,
            [GENERATED_TYPE]: generatedType,
            [DEF_EXP]: defOrExp,
            [CHARSET]: charset,
            [COLLATE]: collate,
            [COMMENT]: comment,
        } = colObj

        if (!this.isCreateTable)
            if (isUpdated) {
                const oldColName = typy(col, `oriObj.${NAME}`).safeString
                sql += `${tokens.change} ${tokens.column} ${quoting(oldColName)} `
            } else sql += `${tokens.add} ${tokens.column} `

        sql += `${quoting(name)}`
        sql += ` ${type}`
        if (un && type !== 'SERIAL') sql += ` ${tokens.un}`
        if (zf) sql += ` ${tokens.zf}`
        if (charset) sql += ` ${tokens.charset} ${charset} ${tokens.collate} ${collate}`
        // when column is generated, NN or NULL can not be defined
        if (generatedType === GENERATED_TYPES.NONE && type !== 'SERIAL')
            sql += ` ${nn ? tokens.nn : tokens.null}`
        if (ai && type !== 'SERIAL') sql += ` ${tokens.ai}`
        if (generatedType === GENERATED_TYPES.NONE && defOrExp)
            sql += ` ${tokens.default} ${defOrExp}`
        else if (defOrExp) sql += ` ${tokens.generated} (${defOrExp}) ${generatedType}`
        if (comment) sql += ` ${tokens.comment} '${comment}'`
        return sql
    }

    /* This builds DROP column SQL
     * @param {Array} cols - columns need to be removed
     * @returns {String} - returns DROP COLUMN sql
     */
    buildRemovedColSQL(cols) {
        return cols
            .map(row => `DROP COLUMN ${quoting(row[COL_ATTRS.NAME])}`)
            .join(this.handleAddComma())
    }

    /**
     * @param {Array} cols - updated columns
     * @returns {String} - returns CHANGE COLUMN sql
     */
    buildUpdatedColSQL(cols) {
        /**
         * Get diff of updated columns, updated of PK and UQ are ignored
         * as they are handled separately.
         */
        const dfnColsChanged = cols.reduce((arr, col) => {
            const dfnColDiff = col.diff.filter(
                d => d.kind === 'E' && d.path[0] !== COL_ATTRS.PK && d.path[0] !== COL_ATTRS.UQ
            )
            if (dfnColDiff.length) arr.push({ ...col, diff: dfnColDiff })
            return arr
        }, [])
        if (dfnColsChanged.length)
            return dfnColsChanged
                .map(col => this.buildColDfnSQL({ col, isUpdated: true }))
                .join(this.handleAddComma())
    }

    buildAddedColSQL(cols) {
        return cols
            .map(col => this.buildColDfnSQL({ col, isUpdated: false }))
            .join(this.handleAddComma())
    }

    /**
     * If altering an existing table, it returns DROP/ADD PK SQL.
     * Otherwise, it returns just PK definition.
     * @returns {String} - returns PK SQL
     */
    buildPkSQL() {
        const removedKey = typy(this.pkKeyDiffs.get('removed'), '[0]').safeObject
        const updatedKey = typy(this.pkKeyDiffs.get('updated'), '[0]').safeObject
        const addedKey = typy(this.pkKeyDiffs.get('added'), '[0]').safeObject
        const dropSQL = `${tokens.drop} ${tokens.primaryKey}`
        let parts = []
        if (removedKey) parts.push(dropSQL)
        if (addedKey || updatedKey) {
            if (updatedKey) parts.push(dropSQL)
            const indexCols = updatedKey ? updatedKey.newObj.index_cols : addedKey.index_cols
            const colNames = indexCols.map(({ id }) => quoting(this.stagingColNameMap[id]))
            let keyDef = `${tokens.primaryKey} (${colNames.join(this.handleAddComma())})`
            if (!this.isCreateTable) keyDef = `${tokens.add} ${keyDef}`
            parts.push(keyDef)
        }
        return parts.join(this.handleAddComma())
    }
    /**
     * @returns {String} - returns UQ SQL
     */
    buildUniqueKeySQL() {
        const removedKeys = this.uqKeyDiffs.get('removed')
        const updatedKeys = this.uqKeyDiffs.get('updated')
        const addedKeys = this.uqKeyDiffs.get('added')
        let parts = []
        parts = removedKeys.map(({ name }) => `${tokens.drop} ${tokens.key} ${quoting(name)}`)
        addedKeys.forEach(({ name, index_cols }) => {
            const indexColNames = index_cols.map(({ id }) => quoting(this.stagingColNameMap[id]))
            const keyDef = `${tokens.uniqueKey} ${quoting(name)}(${indexColNames.join(
                this.handleAddComma()
            )})`
            if (this.isCreateTable) parts.push(keyDef)
            else parts.push(`${tokens.add} ${keyDef}`)
        })
        updatedKeys.forEach(({ diff: keyDiff, oriObj, newObj }) => {
            keyDiff.forEach(diff => {
                // handle build new composite key when a column is removed from the key
                if (
                    diff.kind === 'A' &&
                    typy(diff, 'item.kind').safeString === 'D' &&
                    typy(diff, 'path[0]').safeString === 'index_cols'
                ) {
                    // drop the composite key
                    parts.push(`${tokens.drop} ${tokens.key} ${quoting(oriObj.name)}`)
                    // build new composite key with the remaining columns
                    const indexColNames = newObj.index_cols.map(({ id }) =>
                        quoting(this.stagingColNameMap[id])
                    )
                    parts.push(
                        `${tokens.add} ${tokens.uniqueKey} ${quoting(
                            newObj.name
                        )}(${indexColNames.join(this.handleAddComma())})`
                    )
                }
            })
        })
        return parts.join(this.handleAddComma())
    }

    /**
     * Build SQL for altering columns and keys
     * @returns {String} - returns column alter sql
     */
    buildAlterColsSql() {
        const base = map2dArr({ fields: this.colAttrs, arr: this.initialColsData })
        const newData = map2dArr({ fields: this.colAttrs, arr: this.stagingColsData })
        const diff = arrOfObjsDiff({ base, newArr: newData, idField: COL_ATTRS.ID })

        const removedCols = diff.get('removed')
        const updatedCols = diff.get('updated')
        const addedCols = diff.get('added')
        let alterSpecs = []
        // Build sql for different diff types
        const removedColSQL = this.buildRemovedColSQL(removedCols)
        const updatedColSQL = this.buildUpdatedColSQL(updatedCols)
        const addedColSQL = this.buildAddedColSQL(addedCols)

        const pkSQL = this.buildPkSQL()
        const uqSQL = this.buildUniqueKeySQL()

        if (removedColSQL) alterSpecs.push(removedColSQL)
        if (updatedColSQL) alterSpecs.push(updatedColSQL)
        if (addedColSQL) alterSpecs.push(addedColSQL)
        if (pkSQL) alterSpecs.push(pkSQL)
        if (uqSQL) alterSpecs.push(uqSQL)

        return alterSpecs.join(this.handleAddComma())
    }

    /**
     * Build SQL for creating columns and keys
     * @returns {String} - returns column definition
     */
    buildCreateColsSql() {
        const data = map2dArr({ fields: this.colAttrs, arr: this.stagingColsData })
        let specs = []
        const addedColSQL = this.buildAddedColSQL(data)
        specs.push(addedColSQL)
        const pkSQL = this.buildPkSQL()
        const uqSql = this.buildUniqueKeySQL()
        if (pkSQL) specs.push(pkSQL)
        if (uqSql) specs.push(uqSql)

        return specs.join(this.handleAddComma())
    }

    buildAlterScript() {
        let sql = `${tokens.alterTable} ${quoting(this.initialSchemaName)}.${quoting(
            this.initialTableName
        )}`
        let parts = [] // part of script which will be separated by commas
        if (this.optionDiffs && this.optionDiffs.length) parts.push(this.buildTblOptsSql())
        if (this.isColsOptsChanged) parts.push(this.buildAlterColsSql())
        sql += parts.join(this.handleAddComma())
        sql = formatSQL(`${sql};`)
        return sql
    }

    buildCreateScript() {
        const { schema, name } = this.stagingDataOptions
        let sql = `CREATE SCHEMA IF NOT EXISTS ${quoting(schema)};`
        sql += `${tokens.createTable} ${quoting(schema)}.${quoting(name)} (`
        sql += `${this.buildCreateColsSql()})`
        sql += this.buildTblOptsSql()
        sql = formatSQL(`${sql};`)
        return sql
    }

    build() {
        if (this.isCreateTable) return this.buildCreateScript()
        return this.buildAlterScript()
    }
}
