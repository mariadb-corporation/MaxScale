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
    constructor({ initialData, stagingData }) {
        this.colAttrs = Object.values(COL_ATTRS)
        this.initialData = initialData
        this.initialSchemaName = initialData.options.schema
        this.initialTableName = initialData.options.name
        this.initialColsData = typy(initialData, 'definitions.cols').safeArray

        this.stagingData = stagingData
        this.stagingColsData = typy(stagingData, 'definitions.cols').safeArray

        this.optionDiffs = deepDiff(initialData.options, stagingData.options)
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
     * @returns {String} - returns alter table_options sql
     */
    buildAlterTblOptSql() {
        let sql = ''
        this.optionDiffs.forEach((diff, i) => {
            sql += this.handleAddComma({ ignore: i === 0 })
            const key = diff.path[0]
            switch (key) {
                case 'name':
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
     * This builds column definition SQL. CHANGE/ADD COLUMN ...
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
     * This builds DROP/ADD PK SQL
     * @returns {String} - returns DROP/ADD PK SQL
     */
    buildPkSQL() {
        let sql = ''
        const dropSQL = `${tokens.drop} ${tokens.primaryKey}`
        if (this.isDroppingPK) {
            sql += dropSQL
        } else if (this.isAddingPK) {
            const keys = this.stagingPkColNames.map(col => quoting(col)).join(', ')
            const addSQL = `${tokens.add} ${tokens.primaryKey} (${keys})`
            if (this.initialPkColNames.length > 0) sql += `${dropSQL}, ${addSQL}`
            else sql += addSQL
        }
        return sql
    }

    /**
     * @param {object} payload.col
     * @param {boolean} payload.isUpdated
     * @returns {String} - returns DROP/ADD UNIQUE KEY SQL
     */
    buildUqSQL({ col, isUpdated }) {
        const { NAME, UQ } = COL_ATTRS
        let colObj = col,
            oldColName
        if (isUpdated) {
            colObj = typy(col, 'newObj').safeObjectOrEmpty
            oldColName = typy(col, `oriObj[${NAME}]`).safeString
        }

        const { [NAME]: newColName, [UQ]: newUqValue } = colObj

        if (newUqValue) {
            const uqName = queryHelper.genUqName(newColName)
            return `${tokens.add} ${tokens.uniqueKey} ${quoting(uqName)} (${quoting(newColName)})`
        }
        if (oldColName) {
            const { name: uqName } = queryHelper.getKeyObjByColNames({
                keys: this.initialKeysData,
                keyType: tokens.uniqueKey,
                colNames: [oldColName],
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
     * This handles build ADD, DROP, CHANGE COLUMN SQL
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
        const updatedUqSQL = this.buildUpdatedUqSQL(updatedCols)
        if (removedColSQL) alterSpecs.push(removedColSQL)
        if (updatedColSQL) alterSpecs.push(updatedColSQL)
        if (addedColSQL) alterSpecs.push(addedColSQL)
        if (updatedUqSQL) alterSpecs.push(updatedUqSQL)
        if (!lodash.isEqual(this.initialPkColNames, this.stagingPkColNames))
            alterSpecs.push(this.buildPkSQL())
        return alterSpecs.join(this.handleAddComma())
    }

    build() {
        let sql = `ALTER TABLE ${quoting(this.initialSchemaName)}.${quoting(this.initialTableName)}`
        let tblOptSql = '',
            colsAlterSql = ''
        if (this.optionDiffs && this.optionDiffs.length) tblOptSql = this.buildAlterTblOptSql()
        if (this.isColsOptsChanged) colsAlterSql = this.buildAlterColsSql()
        sql += tblOptSql
        if (colsAlterSql) {
            if (tblOptSql) sql += this.handleAddComma()
            sql += colsAlterSql
        }
        sql = formatSQL(`${sql};`)
        return sql
    }
}
