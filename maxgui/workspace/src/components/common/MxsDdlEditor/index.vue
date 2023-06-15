<template>
    <div class="relative fill-height">
        <ddl-editor-toolbar
            :height="ddlEditorToolbarHeight"
            :disableRevert="!hasChanged"
            :disableApply="!hasValidChanges"
            :mode="mode"
            @on-apply="onApply"
            @on-revert="onRevert"
        >
            <slot v-for="(_, slot) in $slots" :slot="slot" :name="slot" />
        </ddl-editor-toolbar>
        <ddl-editor-form-ctr
            v-model="stagingData"
            :initialData="data"
            :dim="formDim"
            :mode="mode"
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
import { mapState, mapMutations } from 'vuex'
import DdlEditorFormCtr from '@wsSrc/components/common/MxsDdlEditor/DdlEditorFormCtr.vue'
import DdlEditorToolbar from '@wsSrc/components/common/MxsDdlEditor/DdlEditorToolbar.vue'
import queryHelper from '@wsSrc/store/queryHelper'

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
        mode: { type: String, required: true },
        onExecute: { type: Function, required: true },
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
            tokens: state => state.mxsWorkspace.config.CREATE_TBL_TOKENS,
            GENERATED_TYPES: state => state.mxsWorkspace.config.GENERATED_TYPES,
            exec_sql_dlg: state => state.mxsWorkspace.exec_sql_dlg,
            DDL_EDITOR_MODES: state => state.mxsWorkspace.config.DDL_EDITOR_MODES,
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
            return !this.$helpers.lodash.isEqual(this.initialColsData, this.currColsData)
        },
        currColsData() {
            return this.$typy(this.stagingData, 'definitions.cols').safeArray
        },
        initialColsData() {
            return this.$typy(this.data, 'definitions.cols').safeArray
        },
        initialKeysData() {
            return this.$typy(this.data, 'definitions.keys').safeObjectOrEmpty
        },
        stagingPkColNames() {
            return queryHelper.getColNamesByAttr({
                cols: this.currColsData,
                attr: this.COL_ATTRS.PK,
            })
        },
        initialPkColNames() {
            return queryHelper.getColNamesByAttr({
                cols: this.initialColsData,
                attr: this.COL_ATTRS.PK,
            })
        },
        tableOptsDataDiff() {
            return this.$helpers.deepDiff(this.data.options, this.stagingData.options)
        },
        colAttrs() {
            return Object.values(this.COL_ATTRS)
        },
        isDroppingPK() {
            return this.initialPkColNames.length > 0 && this.stagingPkColNames.length === 0
        },
        isAddingPK() {
            return this.stagingPkColNames.length > 0
        },
    },
    methods: {
        ...mapMutations({ SET_EXEC_SQL_DLG: 'mxsWorkspace/SET_EXEC_SQL_DLG' }),
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
         * This builds column definition SQL. CHANGE/ADD COLUMN ...
         * @param {object} payload.col
         * @param {boolean} payload.isUpdated
         * @returns {String} - returns column definition SQL
         */
        buildColDfnSQL({ col, isUpdated }) {
            const { quotingIdentifier: quoting } = this.$helpers
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
            } = this.COL_ATTRS
            let sql = ''
            const colObj = isUpdated ? this.$typy(col, 'newObj').safeObjectOrEmpty : col

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
                const oldColName = this.$typy(col, `oriObj.${NAME}`).safeString
                sql += `${this.tokens.change} ${this.tokens.column} ${quoting(oldColName)} `
            } else sql += `${this.tokens.add} ${this.tokens.column} `

            sql += `${quoting(name)}`
            sql += ` ${type}`
            if (un && type !== 'SERIAL') sql += ` ${this.tokens.un}`
            if (zf) sql += ` ${this.tokens.zf}`
            if (charset)
                sql += ` ${this.tokens.charset} ${charset} ${this.tokens.collate} ${collate}`
            // when column is generated, NN or NULL can not be defined
            if (generatedType === this.GENERATED_TYPES.NONE && type !== 'SERIAL')
                sql += ` ${nn ? this.tokens.nn : this.tokens.null}`
            if (ai && type !== 'SERIAL') sql += ` ${this.tokens.ai}`
            if (generatedType === this.GENERATED_TYPES.NONE && defOrExp)
                sql += ` ${this.tokens.default} ${defOrExp}`
            else if (defOrExp) sql += ` ${this.tokens.generated} (${defOrExp}) ${generatedType}`
            if (comment) sql += ` ${this.tokens.comment} '${comment}'`
            return sql
        },
        /**
         * This builds DROP/ADD PK SQL
         * @returns {String} - returns DROP/ADD PK SQL
         */
        buildPkSQL() {
            let sql = ''
            const { quotingIdentifier: quoting } = this.$helpers
            const dropPKSQL = `${this.tokens.drop} ${this.tokens.primaryKey}`
            if (this.isDroppingPK) {
                sql += dropPKSQL
            } else if (this.isAddingPK) {
                const keys = this.stagingPkColNames.map(col => quoting(col)).join(', ')
                const addPKsql = `${this.tokens.add} ${this.tokens.primaryKey} (${keys})`
                if (this.initialPkColNames.length > 0) sql += `${dropPKSQL}, ${addPKsql}`
                else sql += addPKsql
            }
            return sql
        },
        /**
         * @param {object} payload.col
         * @param {boolean} payload.isUpdated
         * @returns {String} - returns DROP/ADD UNIQUE KEY SQL
         */
        buildUqSQL({ col, isUpdated }) {
            const { quotingIdentifier: quoting } = this.$helpers
            const { NAME, UQ } = this.COL_ATTRS
            let colObj = col,
                oldColName
            if (isUpdated) {
                colObj = this.$typy(col, 'newObj').safeObjectOrEmpty
                oldColName = this.$typy(col, `oriObj[${NAME}]`).safeString
            }

            const { [NAME]: newColName, [UQ]: newUqValue } = colObj

            if (newUqValue) {
                const uqName = queryHelper.genUqName(newColName)
                return `${this.tokens.add} ${this.tokens.uniqueKey} ${quoting(uqName)} (${quoting(
                    newColName
                )})`
            }
            if (oldColName) {
                const { name: uqName } = queryHelper.getKeyObjByColNames({
                    keys: this.initialKeysData,
                    keyType: this.tokens.uniqueKey,
                    colNames: [oldColName],
                })
                return `${this.tokens.drop} ${this.tokens.key} ${quoting(uqName)}`
            }
        },
        /**
         * This builds DROP column SQL
         * @param {Array} cols - columns need to be removed
         * @returns {String} - returns DROP COLUMN sql
         */
        buildRemovedColSQL(cols) {
            const { quotingIdentifier: quoting } = this.$helpers
            return cols
                .map(row => `DROP COLUMN ${quoting(row[this.COL_ATTRS.NAME])}`)
                .join(this.handleAddComma())
        },
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
                    d =>
                        d.kind === 'E' &&
                        d.path[0] !== this.COL_ATTRS.PK &&
                        d.path[0] !== this.COL_ATTRS.UQ
                )
                if (dfnColDiff.length) arr.push({ ...col, diff: dfnColDiff })
                return arr
            }, [])
            if (dfnColsChanged.length)
                return dfnColsChanged
                    .map(col => this.buildColDfnSQL({ col, isUpdated: true }))
                    .join(this.handleAddComma())
        },
        buildAddedColSQL(cols) {
            let colSQL = [],
                uqSQL = []
            cols.forEach(col => {
                colSQL.push(this.buildColDfnSQL({ col, isUpdated: false }))
                const uqSql = this.buildUqSQL({ col, isUpdated: false })
                if (uqSql) uqSQL.push(uqSql)
            })
            return [...colSQL, ...uqSQL].join(this.handleAddComma())
        },
        /**
         * @param {Array} cols - updated columns
         * @returns {String} - returns CHANGE COLUMN sql
         */
        buildUpdatedUqSQL(cols) {
            // Get diff of updated columns, updated of PK is ignored
            const uqColsChanged = cols.reduce((arr, col) => {
                const dfnColDiff = col.diff.filter(
                    d => d.kind === 'E' && d.path[0] === this.COL_ATTRS.UQ
                )
                if (dfnColDiff.length) arr.push({ ...col, diff: dfnColDiff })
                return arr
            }, [])
            if (uqColsChanged.length)
                return uqColsChanged
                    .map(col => this.buildUqSQL({ col, isUpdated: true }))
                    .join(this.handleAddComma())
        },
        /**
         * This handles build ADD, DROP, CHANGE COLUMN SQL
         * @returns {String} - returns column alter sql
         */
        buildColsAlterSQL() {
            const {
                arrOfObjsDiff,
                map2dArr,
                lodash: { isEqual },
            } = this.$helpers
            const base = map2dArr({ fields: this.colAttrs, arr: this.initialColsData })
            const newData = map2dArr({ fields: this.colAttrs, arr: this.currColsData })
            const diff = arrOfObjsDiff({ base, newArr: newData, idField: this.COL_ATTRS.ID })

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
            if (!isEqual(this.initialPkColNames, this.stagingPkColNames))
                alterSpecs.push(this.buildPkSQL())
            return alterSpecs.join(this.handleAddComma())
        },
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
        buildCreateScript() {
            //TODO: Add create script
            return ''
        },
        onRevert() {
            this.stagingData = this.$helpers.lodash.cloneDeep(this.data)
        },
        onApply() {
            this.SET_EXEC_SQL_DLG({
                ...this.exec_sql_dlg,
                is_opened: true,
                sql:
                    this.mode === this.DDL_EDITOR_MODES.ALTER
                        ? this.buildAlterScript()
                        : this.buildCreateScript(),
                on_exec: this.onExecute,
                on_after_cancel: () =>
                    this.SET_EXEC_SQL_DLG({ ...this.exec_sql_dlg, result: null }),
            })
        },
    },
}
</script>
