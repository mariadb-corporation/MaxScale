<template>
    <div class="fill-height">
        <v-card
            v-if="isLoading"
            class="fill-height color border-right-table-border border-bottom-table-border"
            :loading="isLoading"
        />
        <!-- Use v-show to always render it so that multiple worksheets would be kept alive -->
        <div
            v-show="!isLoading"
            class="relative fill-height color border-right-table-border border-bottom-table-border"
        >
            <!-- Only render the portal when component is activated otherwise it has function reference issue -->
            <portal v-if="activated" to="wke-toolbar-right">
                <alter-table-toolbar
                    :disableRevert="!hasChanged"
                    :disableApply="!hasValidChanges"
                    @on-revert="revertChanges"
                    @on-apply="applyChanges"
                />
            </portal>

            <v-tooltip
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
            >
                <template v-slot:activator="{ on }">
                    <v-btn icon small class=" ddl-editor-close" v-on="on" @click="closeDDLEditor">
                        <v-icon size="12" color="navigation"> $vuetify.icons.close</v-icon>
                    </v-btn>
                </template>
                <span>{{ $t('closeDDLEditor') }}</span>
            </v-tooltip>

            <ddl-editor-form
                v-model="formData"
                :dynDim="formDim"
                @is-form-valid="isFormValid = $event"
            />
            <execute-sql-dialog
                v-model="isConfDlgOpened"
                :title="
                    isAlterFailed
                        ? $tc('errors.failedToExeStatements', stmtI18nPluralization)
                        : $tc('confirmations.exeStatements', stmtI18nPluralization)
                "
                :smallInfo="
                    isAlterFailed ? '' : $tc('info.exeStatementsInfo', stmtI18nPluralization)
                "
                :hasSavingErr="isAlterFailed"
                :executedSql="alterSql"
                :errMsgObj="stmtErrMsgObj"
                :sqlTobeExecuted.sync="sql"
                :onSave="confirmAlter"
                @after-close="clearAlterResult"
                @after-cancel="clearAlterResult"
            />
        </div>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapGetters, mapMutations, mapState } from 'vuex'
import DDLEditorForm from './DDLEditorForm.vue'
import AlterTableToolbar from './AlterTableToolbar.vue'
import ExecuteSqlDialog from './ExecuteSqlDialog.vue'
export default {
    name: 'ddl-editor-container',
    components: {
        'ddl-editor-form': DDLEditorForm,
        'alter-table-toolbar': AlterTableToolbar,
        'execute-sql-dialog': ExecuteSqlDialog,
    },
    props: {
        dynDim: { type: Object, required: true },
    },
    data() {
        return {
            formData: {},
            isFormValid: true,
            sql: '',
            activated: true,
            isConfDlgOpened: false,
        }
    },
    computed: {
        ...mapState({
            SQL_EDITOR_MODES: state => state.app_config.SQL_EDITOR_MODES,
            active_wke_id: state => state.query.active_wke_id,
        }),
        ...mapGetters({
            getLoadingTblCreationInfo: 'query/getLoadingTblCreationInfo',
            getTblCreationInfo: 'query/getTblCreationInfo',
            getExeStmtResultMap: 'query/getExeStmtResultMap',
            getDbCmplList: 'query/getDbCmplList',
        }),
        formDim() {
            // title height: 36, border thickness: 2
            return { ...this.dynDim, height: this.dynDim.height - 36 - 2 }
        },
        isLoading() {
            return Boolean(this.getLoadingTblCreationInfo && !this.initialData)
        },
        initialData() {
            return this.$typy(this.getTblCreationInfo, 'data').safeObject
        },
        hasChanged() {
            return !this.$help.lodash.isEqual(
                this.$typy(this.initialData).safeObject,
                this.$typy(this.formData).safeObject
            )
        },
        hasValidChanges() {
            return this.isFormValid && this.hasChanged
        },
        isTblOptsChanged() {
            return !this.$help.lodash.isEqual(
                this.$typy(this.initialData, 'table_opts_data').safeObject,
                this.$typy(this.formData, 'table_opts_data').safeObject
            )
        },
        isColsOptsChanged() {
            return !this.$help.lodash.isEqual(
                this.$typy(this.initialData, 'cols_opts_data.data').safeArray,
                this.$typy(this.formData, 'cols_opts_data.data').safeArray
            )
        },
        notAlteredYet() {
            return Boolean(this.$typy(this.getExeStmtResultMap).isEmptyObject)
        },
        isAlterFailed() {
            if (this.notAlteredYet) return false
            return !this.$typy(this.stmtErrMsgObj).isEmptyObject
        },
        stmtErrMsgObj() {
            return this.$typy(this.getExeStmtResultMap, 'stmt_err_msg_obj').safeObjectOrEmpty
        },
        alterSql() {
            return this.$typy(this.getExeStmtResultMap, 'data.sql').safeString
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
        stmtI18nPluralization() {
            const statementCounts = (this.sql.match(/;/g) || []).length
            return statementCounts > 1 ? 2 : 1
        },
    },
    activated() {
        this.addInitialDataWatcher()
        this.activated = true
    },
    deactivated() {
        this.rmInitialDataWatcher()
        this.activated = false
    },
    methods: {
        ...mapMutations({
            UPDATE_EXE_STMT_RESULT_MAP: 'query/UPDATE_EXE_STMT_RESULT_MAP',
            UPDATE_TBL_CREATION_INFO_MAP: 'query/UPDATE_TBL_CREATION_INFO_MAP',
            UPDATE_CURR_EDITOR_MODE_MAP: 'query/UPDATE_CURR_EDITOR_MODE_MAP',
        }),
        ...mapActions({
            exeStmtAction: 'query/exeStmtAction',
        }),
        //Watcher to work with multiple worksheets which are kept alive
        addInitialDataWatcher() {
            this.rmInitialDataWatcher = this.$watch(
                'initialData',
                v => {
                    if (v && !this.$help.lodash.isEqual(this.formData, v)) {
                        this.formData = this.$help.lodash.cloneDeep(v)
                    }
                },
                {
                    deep: true,
                }
            )
        },
        closeDDLEditor() {
            // Clear altered active node
            this.UPDATE_TBL_CREATION_INFO_MAP({
                id: this.active_wke_id,
                payload: {
                    altered_active_node: null,
                },
            })
            this.UPDATE_CURR_EDITOR_MODE_MAP({
                id: this.active_wke_id,
                payload: this.SQL_EDITOR_MODES.TXT_EDITOR,
            })
        },
        revertChanges() {
            this.formData = this.$help.lodash.cloneDeep(this.initialData)
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
            const { escapeIdentifiers: escape, deepDiff } = this.$help
            const diffs = deepDiff(this.initialData.table_opts_data, this.formData.table_opts_data)
            diffs.forEach((diff, i) => {
                sql += this.handleAddComma({ ignore: i === 0 })
                const key = diff.path[0]
                switch (key) {
                    case 'table_name':
                        sql += `RENAME TO ${escape(dbName)}.${escape(diff.rhs)}`
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
            const { escapeIdentifiers: escape } = this.$help
            removedCols.forEach((row, i) => {
                sql += this.handleAddComma({ ignore: i === 0 })
                sql += `DROP COLUMN ${escape(row.column_name)}`
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
            const { escapeIdentifiers: escape } = this.$help
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
                    sql += `CHANGE COLUMN ${escape(old_column_name)} `
                } else sql += 'ADD COLUMN '

                sql += `${escape(column_name)}`
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
            const { escapeIdentifiers: escape } = this.$help
            let sql = ''
            sql += this.buildColsDfnSQL({ cols: addedCols, isChanging: false })
            addedCols.forEach(({ UQ, column_name }) => {
                if (UQ) {
                    sql += this.handleAddComma()
                    sql += `ADD UNIQUE INDEX ${escape(UQ)} (${escape(column_name)})`
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
            const { escapeIdentifiers: escape } = this.$help
            const dropPKSQL = 'DROP PRIMARY KEY'
            const isDroppingPK = this.initialPkCols.length > 0 && this.currPkCols.length === 0
            const isAddingPK = this.currPkCols.length > 0
            if (isDroppingPK) {
                sql += dropPKSQL
            } else if (isAddingPK) {
                const keys = this.currPkCols.map(col => escape(col)).join(', ')
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
            const { escapeIdentifiers: escape } = this.$help
            uqColsChanged.forEach((col, i) => {
                sql += this.handleAddComma({ ignore: i === 0 })
                const { column_name } = col.newObj
                col.diff.forEach(d => {
                    if (!d.lhs) sql += `ADD UNIQUE INDEX ${escape(d.rhs)} (${escape(column_name)})`
                    else if (!d.rhs) sql += `DROP INDEX ${escape(d.lhs)}`
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
            } = this.$help
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
            const { escapeIdentifiers: escape, formatSQL } = this.$help
            const { dbName, table_name: initialTblName } = this.initialData.table_opts_data
            let sql = `ALTER TABLE ${escape(dbName)}.${escape(initialTblName)}`
            let tblOptSql = '',
                colsAlterSql = ''
            if (this.isTblOptsChanged) tblOptSql = this.buildTblOptSql({ sql, dbName })
            if (this.isColsOptsChanged) colsAlterSql = this.buildColsAlterSQL()
            sql += tblOptSql
            if (colsAlterSql) {
                if (tblOptSql) sql += this.handleAddComma()
                sql += colsAlterSql
            }
            this.sql = formatSQL(`${sql};`)
            this.isConfDlgOpened = true
        },
        async confirmAlter() {
            const { escapeIdentifiers: escape } = this.$help
            const { dbName, table_name } = this.formData.table_opts_data
            await this.exeStmtAction({
                sql: this.sql,
                action: `Apply changes to ${escape(dbName)}.${escape(table_name)}`,
            })
            if (!this.isAlterFailed)
                this.UPDATE_TBL_CREATION_INFO_MAP({
                    id: this.active_wke_id,
                    payload: {
                        data: this.$help.lodash.cloneDeep(this.formData),
                    },
                })
        },
        clearAlterResult() {
            this.UPDATE_EXE_STMT_RESULT_MAP({ id: this.active_wke_id })
        },
    },
}
</script>
<style lang="scss" scoped>
.ddl-editor-close {
    position: absolute;
    top: 4px;
    right: 6px;
}
</style>
