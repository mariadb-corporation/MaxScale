<template>
    <v-card class="ddl-editor-ctr fill-height" :loading="isLoading" tile>
        <div v-show="!isLoading" class="relative fill-height">
            <ddl-editor-toolbar
                :height="ddlEditorToolbarHeight"
                :disableRevert="!hasChanged"
                :disableApply="!hasValidChanges"
                @on-revert="revertChanges"
                @on-apply="applyChanges"
            />
            <ddl-editor-form-ctr
                v-model="formData"
                :initialData="initialData"
                :dim="formDim"
                @is-form-valid="isFormValid = $event"
            />
        </div>
    </v-card>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-07-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
 * Events
 * 2-way data binding to execSqlDlg prop
 * update:execSqlDlg?: (object)
 */

import Editor from '@wsModels/Editor'
import QueryEditor from '@wsModels/QueryEditor'
import QueryEditorTmp from '@wsModels/QueryEditorTmp'
import DdlEditorFormCtr from '@wkeComps/QueryEditor/DdlEditorFormCtr.vue'
import DdlEditorToolbar from '@wkeComps/QueryEditor/DdlEditorToolbar.vue'

export default {
    name: 'ddl-editor-ctr',
    components: {
        DdlEditorFormCtr,
        DdlEditorToolbar,
    },
    props: {
        dim: { type: Object, required: true },
        execSqlDlg: { type: Object, required: true },
        isExecFailed: { type: Boolean, required: true },
    },
    data() {
        return {
            ddlEditorToolbarHeight: 28,
            /**
             * TODO: move formData to SchemaSidebar model so that the changes can be kept
             * after page refresh, changing active queryTab or worksheet.
             */
            formData: {},
            isFormValid: true,
        }
    },
    computed: {
        tblCreationInfo() {
            return Editor.getters('getTblCreationInfo')
        },
        formDim() {
            return { ...this.dim, height: this.dim.height - this.ddlEditorToolbarHeight }
        },
        isLoading() {
            return Boolean(
                Editor.getters('getLoadingTblCreationInfo') &&
                    this.$typy(this.initialData).isEmptyObject
            )
        },
        initialData() {
            return this.$typy(this.tblCreationInfo, 'data').safeObjectOrEmpty
        },
        hasChanged() {
            return !this.$helpers.lodash.isEqual(
                this.$typy(this.initialData).safeObject,
                this.$typy(this.formData).safeObject
            )
        },
        hasValidChanges() {
            return this.isFormValid && this.hasChanged
        },
        isTblOptsChanged() {
            return !this.$helpers.lodash.isEqual(
                this.$typy(this.initialData, 'table_opts_data').safeObject,
                this.$typy(this.formData, 'table_opts_data').safeObject
            )
        },
        isColsOptsChanged() {
            return !this.$helpers.lodash.isEqual(
                this.$typy(this.initialData, 'cols_opts_data.data').safeArray,
                this.$typy(this.formData, 'cols_opts_data.data').safeArray
            )
        },
        currColsData() {
            return this.$typy(this.formData, 'cols_opts_data.data').safeArray
        },
        initialColsData() {
            return this.$typy(this.initialData, 'cols_opts_data.data').safeArray
        },
        currPkCols() {
            return this.getPKCols(this.currColsData)
        },
        initialPkCols() {
            return this.getPKCols(this.initialColsData)
        },
    },
    activated() {
        this.watch_initialData()
    },
    deactivated() {
        this.$typy(this.unwatch_initialData).safeFunction()
    },
    methods: {
        //Watcher to work with multiple worksheets which are kept alive
        watch_initialData() {
            this.unwatch_initialData = this.$watch(
                'initialData',
                v => {
                    if (v && !this.$helpers.lodash.isEqual(this.formData, v)) {
                        this.formData = this.$helpers.lodash.cloneDeep(v)
                    }
                },
                { deep: true, immediate: true }
            )
        },
        revertChanges() {
            this.formData = this.$helpers.lodash.cloneDeep(this.initialData)
        },
        getPKCols(colsData) {
            const headers = this.$typy(this.formData, 'cols_opts_data.fields').safeArray
            let cols = []
            const idxOfPk = headers.findIndex(h => h === 'PK')
            const idxOfColumnName = headers.findIndex(h => h === 'column_name')
            colsData.forEach(row => {
                if (row[idxOfPk] === 'YES') cols.push(row[idxOfColumnName])
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
         * @param {String} payload.dbName - db name
         * @returns {String} - returns alter table_options sql
         */
        buildTblOptSql({ dbName }) {
            let sql = ''
            const { quotingIdentifier: quoting, deepDiff } = this.$helpers
            const diffs = deepDiff(this.initialData.table_opts_data, this.formData.table_opts_data)
            diffs.forEach((diff, i) => {
                sql += this.handleAddComma({ ignore: i === 0 })
                const key = diff.path[0]
                switch (key) {
                    case 'table_name':
                        sql += `RENAME TO ${quoting(dbName)}.${quoting(diff.rhs)}`
                        break
                    case 'table_engine':
                        sql += `ENGINE = ${diff.rhs}`
                        break
                    case 'table_charset':
                        sql += `CHARACTER SET = ${diff.rhs}`
                        break
                    case 'table_collation':
                        sql += `COLLATE = ${diff.rhs}`
                        break
                    case 'table_comment':
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
                sql += `DROP COLUMN ${quoting(row.column_name)}`
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
            cols.forEach((col, i) => {
                sql += this.handleAddComma({ ignore: i === 0 })
                const colObj = isChanging ? this.$typy(col, 'newObj').safeObject : col
                const {
                    column_name,
                    column_type,
                    UN,
                    ZF,
                    NN,
                    AI,
                    generated,
                    charset,
                    collation,
                    'default/expression': defOrExp,
                    comment,
                } = colObj
                if (isChanging) {
                    const old_column_name = this.$typy(col, 'oriObj.column_name').safeString
                    sql += `CHANGE COLUMN ${quoting(old_column_name)} `
                } else sql += 'ADD COLUMN '

                sql += `${quoting(column_name)}`
                sql += ` ${column_type}`
                if (UN) sql += ` ${UN}`
                if (ZF) sql += ` ${ZF}`
                if (charset) sql += ` CHARACTER SET ${charset} COLLATE ${collation}`
                // when column is generated, NN or NULL can not be defined
                if (NN && generated === '(none)') sql += ` ${NN}`
                if (AI) sql += ` ${AI}`
                if (generated === '(none)' && defOrExp) sql += ` DEFAULT ${defOrExp}`
                else if (defOrExp) sql += ` AS (${defOrExp}) ${generated}`
                if (comment) sql += ` COMMENT '${comment}'`
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
            addedCols.forEach(({ UQ, column_name }) => {
                if (UQ) {
                    sql += this.handleAddComma()
                    sql += `ADD UNIQUE INDEX ${quoting(UQ)} (${quoting(column_name)})`
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
            const dropPKSQL = 'DROP PRIMARY KEY'
            const isDroppingPK = this.initialPkCols.length > 0 && this.currPkCols.length === 0
            const isAddingPK = this.currPkCols.length > 0
            if (isDroppingPK) {
                sql += dropPKSQL
            } else if (isAddingPK) {
                const keys = this.currPkCols.map(col => quoting(col)).join(', ')
                const addPKsql = `ADD PRIMARY KEY (${keys})`
                if (this.initialPkCols.length > 0) sql += `${dropPKSQL}, ${addPKsql}`
                else sql += addPKsql
            }
            return sql
        },
        /**
         * This builds DROP/ADD UNIQUE INDEX SQL
         * @param {Array} payload.uqColsChanged - columns have UQ value changed
         * @returns {String} - returns DROP/ADD UNIQUE INDEX SQL
         */
        buildUQSQL({ uqColsChanged }) {
            let sql = ''
            const { quotingIdentifier: quoting } = this.$helpers
            uqColsChanged.forEach((col, i) => {
                sql += this.handleAddComma({ ignore: i === 0 })
                const { column_name } = col.newObj
                col.diff.forEach(d => {
                    if (!d.lhs)
                        sql += `ADD UNIQUE INDEX ${quoting(d.rhs)} (${quoting(column_name)})`
                    else if (!d.rhs) sql += `DROP INDEX ${quoting(d.lhs)}`
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
            let sql = '',
                colDfnSQL = '',
                uqSQL = ''
            /**
             * iterates through all updatedCols and keep cols having column definition, UQ changed
             * This also filters diff
             */
            const uqColsChanged = updatedCols.reduce((arr, col) => {
                const uqColDiff = col.diff.filter(d => d.kind === 'E' && d.path[0] === 'UQ')
                if (uqColDiff.length) arr.push({ ...col, diff: uqColDiff })
                return arr
            }, [])
            const dfnColsChanged = updatedCols.reduce((arr, col) => {
                const dfnColDiff = col.diff.filter(
                    d => d.kind === 'E' && d.path[0] !== 'PK' && d.path[0] !== 'UQ'
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
            let sql = '',
                pkSQL = ''
            const {
                arrOfObjsDiff,
                getObjectRows,
                lodash: { isEqual },
            } = this.$helpers
            const base = getObjectRows({
                columns: this.$typy(this.initialData, 'cols_opts_data.fields').safeArray,
                rows: this.$typy(this.initialData, 'cols_opts_data.data').safeArray,
            })
            const newData = getObjectRows({
                columns: this.$typy(this.formData, 'cols_opts_data.fields').safeArray,
                rows: this.$typy(this.formData, 'cols_opts_data.data').safeArray,
            })
            const diff = arrOfObjsDiff({ base, newArr: newData, idField: 'id' })
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
        applyChanges() {
            const { quotingIdentifier: quoting, formatSQL } = this.$helpers
            const { dbName, table_name: initialTblName } = this.initialData.table_opts_data
            let sql = `ALTER TABLE ${quoting(dbName)}.${quoting(initialTblName)}`
            let tblOptSql = '',
                colsAlterSql = ''
            if (this.isTblOptsChanged) tblOptSql = this.buildTblOptSql({ sql, dbName })
            if (this.isColsOptsChanged) colsAlterSql = this.buildColsAlterSQL()
            sql += tblOptSql
            if (colsAlterSql) {
                if (tblOptSql) sql += this.handleAddComma()
                sql += colsAlterSql
            }
            this.$emit('update:execSqlDlg', {
                ...this.execSqlDlg,
                isOpened: true,
                sql: formatSQL(`${sql};`),
                onExec: this.confirmAlter,
                onAfterClose: this.clearAlterResult,
                onAfterCancel: this.clearAlterResult,
            })
        },
        async confirmAlter() {
            const { quotingIdentifier: quoting } = this.$helpers
            const { dbName, table_name } = this.formData.table_opts_data
            await QueryEditor.dispatch('exeStmtAction', {
                sql: this.execSqlDlg.sql,
                action: `Apply changes to ${quoting(dbName)}.${quoting(table_name)}`,
            })
            if (!this.isExecFailed) {
                const data = this.$helpers.lodash.cloneDeep(this.formData)
                Editor.update({
                    where: QueryEditor.getters('getActiveQueryTabId'),
                    data(editor) {
                        editor.tbl_creation_info.data = data
                    },
                })
            }
        },
        clearAlterResult() {
            QueryEditorTmp.update({
                where: QueryEditor.getters('getQueryEditorId'),
                data: { exe_stmt_result: {} },
            })
        },
    },
}
</script>
<style lang="scss" scoped>
.ddl-editor-ctr {
    box-shadow: none !important;
}
</style>
