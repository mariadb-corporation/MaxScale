<template>
    <div class="relative fill-height">
        <ddl-editor-toolbar
            :height="ddlEditorToolbarHeight"
            :disableRevert="!hasChanged"
            :disableApply="!hasValidChanges"
            v-on="$listeners"
        />
        <ddl-editor-form-ctr
            v-model="stagingData"
            :initialData="data"
            :dim="formDim"
            @is-form-valid="isFormValid = $event"
        />
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
/*
 * Emits:
 * - $emit('on-revert')
 * - $emit('on-apply')
 */
import { mapState } from 'vuex'
import DdlEditorFormCtr from '@wsSrc/components/common/MxsDdlEditor/DdlEditorFormCtr.vue'
import DdlEditorToolbar from '@wsSrc/components/common/MxsDdlEditor/DdlEditorToolbar.vue'

export default {
    name: 'mxs-ddl-editor',
    components: {
        DdlEditorFormCtr,
        DdlEditorToolbar,
    },
    props: {
        value: { type: Object, required: true },
        dim: { type: Object, required: true },
        data: { type: Object, required: true },
    },
    data() {
        return {
            ddlEditorToolbarHeight: 28,
            isFormValid: true,
        }
    },
    computed: {
        ...mapState({
            COL_ATTRS: state => state.mxsWorkspace.config.COL_ATTRS,
            COL_ATTR_IDX_MAP: state => state.mxsWorkspace.config.COL_ATTR_IDX_MAP,
            tokens: state => state.mxsWorkspace.config.CREATE_TBL_TOKENS,
            GENERATED_TYPES: state => state.mxsWorkspace.config.GENERATED_TYPES,
        }),
        stagingData: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
        formDim() {
            return { ...this.dim, height: this.dim.height - this.ddlEditorToolbarHeight }
        },
        hasChanged() {
            return !this.$helpers.lodash.isEqual(this.data, this.stagingData)
        },
        hasValidChanges() {
            return this.isFormValid && this.hasChanged
        },
        isTblOptsChanged() {
            return !this.$helpers.lodash.isEqual(
                this.$typy(this.data, 'options').safeObject,
                this.$typy(this.stagingData, 'options').safeObject
            )
        },
        isColsOptsChanged() {
            return !this.$helpers.lodash.isEqual(
                this.$typy(this.data, 'definitions.data').safeArray,
                this.$typy(this.stagingData, 'definitions.data').safeArray
            )
        },
        currColsData() {
            return this.$typy(this.stagingData, 'definitions.data').safeArray
        },
        initialColsData() {
            return this.$typy(this.data, 'definitions.data').safeArray
        },
        currPkCols() {
            return this.getPKCols(this.currColsData)
        },
        initialPkCols() {
            return this.getPKCols(this.initialColsData)
        },
        tableOptsDataDiff() {
            return this.$helpers.deepDiff(this.data.options, this.stagingData.options)
        },
        colAttrs() {
            return Object.values(this.COL_ATTRS)
        },
        idxOfPk() {
            return this.COL_ATTR_IDX_MAP[this.COL_ATTRS.PK]
        },
        idxOfColumnName() {
            return this.COL_ATTR_IDX_MAP[this.COL_ATTRS.NAME]
        },
    },
    methods: {
        getPKCols(colsData) {
            let cols = []
            colsData.forEach(row => {
                if (row[this.idxOfPk]) cols.push(row[this.idxOfColumnName])
            })
            return cols
        },
        /**
         * @param {Boolean} payload.ignore - ignore adding comma
         * @returns {String} - return ', ' or ''
         */
        handleAddComma: ({ ignore = false } = {}) => (ignore ? '' : ', '),
        /**
         * This builds table_options SQL
         * @returns {String} - returns alter table_options sql
         */
        buildTblOptSql() {
            let sql = ''
            const { schema } = this.data.options
            const { quotingIdentifier: quoting } = this.$helpers
            this.tableOptsDataDiff.forEach((diff, i) => {
                sql += this.handleAddComma({ ignore: i === 0 })
                const key = diff.path[0]
                switch (key) {
                    case 'name':
                        sql += `RENAME TO ${quoting(schema)}.${quoting(diff.rhs)}`
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
        },
        /**
         * This builds DROP column SQL
         * @param {Array} payload.removedCols - columns need to be dropped
         * @returns {String} - returns DROP COLUMN sql
         */
        buildDropColSql({ removedCols }) {
            let sql = ''
            const { quotingIdentifier: quoting } = this.$helpers
            removedCols.forEach((row, i) => {
                sql += this.handleAddComma({ ignore: i === 0 })
                sql += `DROP COLUMN ${quoting(row[this.COL_ATTRS.NAME])}`
            })
            return sql
        },
        /**
         * This builds column definition SQL. Either CHANGE COLUMN or ADD COLUMN
         * @param {Array} payload.cols - cols: either diff cols from arrOfObjsDiff or new cols
         * @param {Boolean} payload.isChanging - is changing column
         * @returns {String} - returns column definition SQL
         */
        buildColsDfnSQL({ cols, isChanging }) {
            let sql = ''
            const { quotingIdentifier: quoting } = this.$helpers
            const {
                NAME,
                TYPE,
                UN,
                ZF,
                NN,
                AI,
                GENERATED_TYPE,
                CHARSET,
                COLLATE,
                DEF_EXP,
                COMMENT,
            } = this.COL_ATTRS

            cols.forEach((col, i) => {
                sql += this.handleAddComma({ ignore: i === 0 })
                const colObj = isChanging ? this.$typy(col, 'newObj').safeObject : col
                const {
                    [NAME]: name,
                    [TYPE]: type,
                    [UN]: un,
                    [ZF]: zf,
                    [NN]: nn,
                    [AI]: ai,
                    [GENERATED_TYPE]: generatedType,
                    [CHARSET]: charset,
                    [COLLATE]: collate,
                    [DEF_EXP]: defOrExp,
                    [COMMENT]: comment,
                } = colObj

                if (isChanging) {
                    const old_column_name = this.$typy(col, `oriObj.${NAME}`).safeString
                    sql += `${this.tokens.change} ${this.tokens.column} ${quoting(
                        old_column_name
                    )} `
                } else sql += `${this.tokens.add} ${this.tokens.column} `

                sql += `${quoting(name)}`
                sql += ` ${type}`
                if (un) sql += ` ${this.tokens.un}`
                if (zf) sql += ` ${this.tokens.zf}`
                if (charset)
                    sql += ` ${this.tokens.charset} ${charset} ${this.tokens.collate} ${collate}`
                // when column is generated, NN or NULL can not be defined
                if (generatedType === this.GENERATED_TYPES.NONE)
                    sql += ` ${nn ? this.tokens.nn : this.tokens.null}`
                if (ai) sql += ` ${this.tokens.ai}`
                if (generatedType === this.GENERATED_TYPES.NONE && defOrExp)
                    sql += ` ${this.tokens.default} ${defOrExp}`
                else if (defOrExp) sql += ` ${this.tokens.generated} (${defOrExp}) ${generatedType}`
                if (comment) sql += ` ${this.tokens.comment} '${comment}'`
            })
            return sql
        },
        /**
         * This builds ADD COLUMN SQL
         * @param {Array} payload.addedCols - columns need to be added
         * @returns {String} - returns ADD COLUMN sql
         */
        buildAddColSQL({ addedCols }) {
            const { quotingIdentifier: quoting } = this.$helpers
            let sql = ''
            sql += this.buildColsDfnSQL({ cols: addedCols, isChanging: false })
            const { UQ, NAME } = this.COL_ATTRS
            addedCols.forEach(col => {
                if (col[UQ]) {
                    sql += this.handleAddComma()
                    sql += `${this.tokens.add} ${this.tokens.uniqueKey} ${quoting(UQ)} (${quoting(
                        col[NAME]
                    )})`
                }
            })
            return sql
        },

        /**
         * This builds DROP/ADD PK SQL
         * @returns {String} - returns DROP/ADD PK SQL
         */
        buildPKSQL() {
            let sql = ''
            const { quotingIdentifier: quoting } = this.$helpers
            const dropPKSQL = `${this.tokens.drop} ${this.tokens.primaryKey}`
            const isDroppingPK = this.initialPkCols.length > 0 && this.currPkCols.length === 0
            const isAddingPK = this.currPkCols.length > 0
            if (isDroppingPK) {
                sql += dropPKSQL
            } else if (isAddingPK) {
                const keys = this.currPkCols.map(col => quoting(col)).join(', ')
                const addPKsql = `${this.tokens.add} ${this.tokens.primaryKey} (${keys})`
                if (this.initialPkCols.length > 0) sql += `${dropPKSQL}, ${addPKsql}`
                else sql += addPKsql
            }
            return sql
        },
        /**
         * This builds DROP/ADD UNIQUE KEY SQL
         * @param {Array} payload.uqColsChanged - columns have UQ value changed
         * @returns {String} - returns DROP/ADD UNIQUE KEY SQL
         */
        buildUQSQL({ uqColsChanged }) {
            let sql = ''
            const { quotingIdentifier: quoting } = this.$helpers
            uqColsChanged.forEach((col, i) => {
                sql += this.handleAddComma({ ignore: i === 0 })
                col.diff.forEach(d => {
                    if (!d.lhs)
                        sql += `${this.tokens.add} ${this.tokens.uniqueKey} ${quoting(
                            d.rhs
                        )} (${quoting(col.newObj[this.COL_ATTRS.NAME])})`
                    else if (!d.rhs)
                        sql += `${this.tokens.drop} ${this.tokens.key} ${quoting(d.lhs)}`
                })
            })
            return sql
        },
        /**
         * This handles build column definition and column constraints SQL
         * @param {Array} payload.updatedCols - columns need to be changed
         * @returns {String} - returns CHANGE COLUMN sql
         */
        buildChangeColSQL({ updatedCols }) {
            const { PK, UQ } = this.COL_ATTRS
            let sql = '',
                colDfnSQL = '',
                uqSQL = ''
            /**
             * iterates through all updatedCols and keep cols having column definition, UQ changed
             * This also filters diff
             */
            const uqColsChanged = updatedCols.reduce((arr, col) => {
                const uqColDiff = col.diff.filter(d => d.kind === 'E' && d.path[0] === UQ)
                if (uqColDiff.length) arr.push({ ...col, diff: uqColDiff })
                return arr
            }, [])
            const dfnColsChanged = updatedCols.reduce((arr, col) => {
                const dfnColDiff = col.diff.filter(
                    d => d.kind === 'E' && d.path[0] !== PK && d.path[0] !== UQ
                )
                if (dfnColDiff.length) arr.push({ ...col, diff: dfnColDiff })
                return arr
            }, [])
            // build sql
            if (dfnColsChanged.length)
                colDfnSQL = this.buildColsDfnSQL({ cols: dfnColsChanged, isChanging: true })
            if (uqColsChanged.length) uqSQL = this.buildUQSQL({ uqColsChanged })

            // handle assign sql
            sql += colDfnSQL
            if (uqSQL) {
                if (colDfnSQL) sql += this.handleAddComma()
                sql += uqSQL
            }
            return sql
        },
        /**
         * This handles build ADD, DROP, CHANGE COLUMN SQL
         * @returns {String} - returns column alter sql
         */
        buildColsAlterSQL() {
            const { ID } = this.COL_ATTRS
            let sql = '',
                pkSQL = ''
            const {
                arrOfObjsDiff,
                getObjectRows,
                lodash: { isEqual },
            } = this.$helpers
            const base = getObjectRows({
                columns: this.colAttrs,
                rows: this.$typy(this.data, 'definitions.data').safeArray,
            })
            const newData = getObjectRows({
                columns: this.colAttrs,
                rows: this.$typy(this.stagingData, 'definitions.data').safeArray,
            })
            const diff = arrOfObjsDiff({ base, newArr: newData, idField: ID })
            const removedCols = diff.get('removed')
            const updatedCols = diff.get('updated')
            const addedCols = diff.get('added')
            // Build sql for different diff types
            let dropColSql = '',
                changeColSql = '',
                addedColSql = ''
            if (removedCols.length) dropColSql = this.buildDropColSql({ removedCols })
            if (updatedCols.length) changeColSql = this.buildChangeColSQL({ updatedCols })
            if (addedCols.length) addedColSql = this.buildAddColSQL({ addedCols })
            if (!isEqual(this.initialPkCols, this.currPkCols)) pkSQL = this.buildPKSQL()
            sql += dropColSql
            if (changeColSql) {
                if (dropColSql) sql += this.handleAddComma()
                sql += changeColSql
            }
            if (addedColSql) {
                if (dropColSql || changeColSql) sql += this.handleAddComma()
                sql += addedColSql
            }
            if (pkSQL) {
                if (dropColSql || changeColSql || addedColSql) sql += this.handleAddComma()
                sql += pkSQL
            }

            return sql
        },
        /**
         * @public
         */
        buildAlterScript() {
            const { quotingIdentifier: quoting, formatSQL } = this.$helpers
            const { schema, name } = this.data.options
            let sql = `ALTER TABLE ${quoting(schema)}.${quoting(name)}`
            let tblOptSql = '',
                colsAlterSql = ''
            if (this.isTblOptsChanged) tblOptSql = this.buildTblOptSql()
            if (this.isColsOptsChanged) colsAlterSql = this.buildColsAlterSQL()
            sql += tblOptSql
            if (colsAlterSql) {
                if (tblOptSql) sql += this.handleAddComma()
                sql += colsAlterSql
            }
            sql = formatSQL(`${sql};`)
            return sql
        },
    },
}
</script>
