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
import queryHelper from '@wsSrc/store/queryHelper'

/**
 * Table script builder.
 * This is designed to work with the output of queryHelper.tableParserTransformer,
 * which is a data structure representing the parsed information of a table.
 */
export default class TableScriptBuilder {
    constructor({ initialData, stagingData, isCreateTable }) {
        // initialData is an empty object if `isCreateTable` is true
        this.colAttrs = Object.values(COL_ATTRS)
        this.initialData = initialData
        this.initialSchemaName = typy(initialData, 'options.schema').safeString
        this.initialTableName = typy(initialData, 'options.name').safeString
        this.initialColsData = typy(initialData, 'definitions.cols').safeArray

        this.stagingData = stagingData
        this.stagingColsData = typy(stagingData, 'definitions.cols').safeArray

        this.optionDiffs = deepDiff(
            typy(initialData, 'options').safeObjectOrEmpty,
            stagingData.options
        )
        this.isColsOptsChanged = !lodash.isEqual(this.initialColsData, this.stagingColsData)

        // Keys
        this.initialKeysData = typy(initialData, 'definitions.keys').safeObjectOrEmpty
        this.initialPkColNames = queryHelper.getColNamesByAttr({
            cols: this.initialColsData,
            attr: COL_ATTRS.PK,
        })
        this.stagingPkColNames = queryHelper.getColNamesByAttr({
            cols: this.stagingColsData,
            attr: COL_ATTRS.PK,
        })
        this.isDroppingPK = this.initialPkColNames.length > 0 && this.stagingPkColNames.length === 0
        this.isAddingPK = this.stagingPkColNames.length > 0

        // mode
        this.isCreateTable = isCreateTable
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

    /**
     * If altering an existing table, it returns DROP/ADD PK SQL.
     * Otherwise, it returns just PK definition.
     * @returns {String} - returns PK SQL
     */
    buildPkSQL() {
        let sql = ''
        const dropSQL = `${tokens.drop} ${tokens.primaryKey}`
        if (this.isDroppingPK) {
            sql += dropSQL
        } else if (this.isAddingPK) {
            const keys = this.stagingPkColNames.map(col => quoting(col)).join(', ')
            const keyDef = `${tokens.primaryKey} (${keys})`

            if (this.isCreateTable) return keyDef

            const addSQL = `${tokens.add} ${keyDef}`
            if (this.initialPkColNames.length > 0) sql += `${dropSQL}, ${addSQL}`
            else sql += addSQL
        }
        return sql
    }

    /**
     * If altering an existing table, it returns DROP/ADD UNIQUE KEY SQL.
     * Otherwise, it returns just UQ key definition.
     * @param {object} payload.col
     * @param {boolean} payload.isUpdated
     * @returns {String} - returns UNIQUE KEY SQL
     */
    buildUqSQL({ col, isUpdated }) {
        const { ID, NAME, UQ } = COL_ATTRS
        let colObj = col,
            colId
        if (isUpdated) {
            colObj = typy(col, 'newObj').safeObjectOrEmpty
            colId = typy(col, `oriObj[${ID}]`).safeString
        }

        const { [NAME]: newColName, [UQ]: newUqValue } = colObj

        if (newUqValue) {
            const uqName = queryHelper.genKeyName({
                colName: newColName,
                category: tokens.uniqueKey,
            })
            const keyDef = `${tokens.uniqueKey} ${quoting(uqName)} (${quoting(newColName)})`
            if (this.isCreateTable) return keyDef
            return `${tokens.add} ${keyDef}`
        }
        if (colId) {
            const { name: uqName } = queryHelper.getKeyObjByColIds({
                keys: this.initialKeysData,
                keyType: tokens.uniqueKey,
                colIds: [colId],
            })
            return `${tokens.drop} ${tokens.key} ${quoting(uqName)}`
        }
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
        let colSQL = [],
            uqSQL = []
        cols.forEach(col => {
            colSQL.push(this.buildColDfnSQL({ col, isUpdated: false }))
            const uqSql = this.buildUqSQL({ col, isUpdated: false })
            if (uqSql) uqSQL.push(uqSql)
        })
        return [...colSQL, ...uqSQL].join(this.handleAddComma())
    }

    /**
     * @param {Array} cols - updated columns
     * @returns {String} - returns CHANGE COLUMN sql
     */
    buildUpdatedUqSQL(cols) {
        // Get diff of updated columns, updated of PK is ignored
        const uqColsChanged = cols.reduce((arr, col) => {
            const dfnColDiff = col.diff.filter(d => d.kind === 'E' && d.path[0] === COL_ATTRS.UQ)
            if (dfnColDiff.length) arr.push({ ...col, diff: dfnColDiff })
            return arr
        }, [])
        if (uqColsChanged.length)
            return uqColsChanged
                .map(col => this.buildUqSQL({ col, isUpdated: true }))
                .join(this.handleAddComma())
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
        //TODO: Build UQ sql based on staging UQ keys to handle also composite UQ keys
        const updatedUqSQL = this.buildUpdatedUqSQL(updatedCols)

        if (removedColSQL) alterSpecs.push(removedColSQL)
        if (updatedColSQL) alterSpecs.push(updatedColSQL)
        if (addedColSQL) alterSpecs.push(addedColSQL)
        if (updatedUqSQL) alterSpecs.push(updatedUqSQL)
        if (!lodash.isEqual(this.initialPkColNames, this.stagingPkColNames))
            alterSpecs.push(this.buildPkSQL())
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
        if (this.stagingPkColNames.length) specs.push(this.buildPkSQL())
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
        const { schema, name } = this.stagingData.options
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
