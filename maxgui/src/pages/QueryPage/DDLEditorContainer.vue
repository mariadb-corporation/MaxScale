<template>
    <div class="fill-height">
        <v-card
            v-if="isLoading"
            class="fill-height color border-top-table-border border-right-table-border border-bottom-table-border"
            :loading="isLoading"
        />
        <!-- Use v-show to always render it so that multiple worksheets would be kept alive -->
        <div
            v-show="!isLoading"
            class="relative fill-height color border-top-table-border border-right-table-border border-bottom-table-border"
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
            <div class="pt-2 pl-3 pr-2 d-flex align-center justify-space-between">
                <span class="text-body-2 color text-navigation font-weight-bold text-uppercase">
                    {{ $t('alterTbl') }}
                </span>
                <v-tooltip
                    top
                    transition="slide-y-transition"
                    content-class="shadow-drop color text-navigation py-1 px-4"
                >
                    <template v-slot:activator="{ on }">
                        <v-btn icon small v-on="on" @click="closeDDLEditor">
                            <v-icon size="12" color="navigation"> $vuetify.icons.close</v-icon>
                        </v-btn>
                    </template>
                    <span>{{ $t('closeDDLEditor') }}</span>
                </v-tooltip>
            </div>
            <ddl-editor-form
                v-model="formData"
                :dynDim="formDim"
                @is-form-valid="isFormValid = $event"
            />
            <confirm-dialog
                ref="confirmAlterDialog"
                :title="
                    isErrDialogShown ? $t('errors.alterFailed') : $t('confirmations.alterTable')
                "
                :smallInfo="isErrDialogShown ? '' : $t('info.alterTableInfo')"
                type="execute"
                :onSave="confirmAlter"
                minBodyWidth="768px"
                :hasSavingErr="isAlterFailed"
                :onCancel="clearAlterResult"
                :onClose="clearAlterResult"
            >
                <template v-slot:body-prepend>
                    <table v-if="isErrDialogShown" class="alter-err-tbl pa-4">
                        <tr>
                            <td><b>sql</b></td>
                            <td>
                                {{ alterSql }}
                            </td>
                        </tr>
                        <tr v-for="(v, key) in alterResult" :key="key">
                            <td>
                                <b>{{ key }}</b>
                            </td>
                            <td>
                                {{ v }}
                            </td>
                        </tr>
                    </table>

                    <div
                        v-else
                        class="mb-4 pt-2 pl-2 color border-all-table-border"
                        style="height:250px"
                    >
                        <query-editor
                            v-if="sql"
                            v-model="sql"
                            :class="`fill-height`"
                            :cmplList="getDbCmplList"
                            :options="{
                                fontSize: 10,
                                contextmenu: false,
                            }"
                        />
                    </div>
                </template>
            </confirm-dialog>
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
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapGetters, mapMutations, mapState } from 'vuex'
import DDLEditorForm from './DDLEditorForm.vue'
import AlterTableToolbar from './AlterTableToolbar.vue'
import QueryEditor from '@/components/QueryEditor'
export default {
    name: 'ddl-editor-container',
    components: {
        'ddl-editor-form': DDLEditorForm,
        'alter-table-toolbar': AlterTableToolbar,
        'query-editor': QueryEditor,
    },
    props: {
        dynDim: { type: Object, required: true },
    },
    data() {
        return {
            formData: {},
            isFormValid: true,
            sql: '',
            /**
             * Using isErrDialogShown instead of isAlterFailed because
             * when closing the dialog with error message, the action to clear
             * `isAlterFailed` is dispatched immediately but dialog is still in the transition
             * of closing. As a result, the query-editor is rendered while closing. This
             * state helps to keep error dialog content even when dialog is closed.
             *
             */
            isErrDialogShown: false,
            activated: true,
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
            getAlteringTableResultMap: 'query/getAlteringTableResultMap',
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

        notAlteredYet() {
            return Boolean(this.$typy(this.getAlteringTableResultMap).isEmptyObject)
        },
        isAlterFailed() {
            if (this.notAlteredYet) return false
            return Boolean(this.$typy(this.alterResult, 'errno').safeObject)
        },
        alterResult() {
            return this.$typy(this.getAlteringTableResultMap, 'data.results[0]').safeObject
        },
        alterSql() {
            return this.$typy(this.getAlteringTableResultMap, 'data.sql').safeString
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
            UPDATE_ALTERING_TABLE_RESULT_MAP: 'query/UPDATE_ALTERING_TABLE_RESULT_MAP',
            UPDATE_TBL_CREATION_INFO_MAP: 'query/UPDATE_TBL_CREATION_INFO_MAP',
            SET_CURR_EDITOR_MODE_MAP: 'query/SET_CURR_EDITOR_MODE_MAP',
        }),
        ...mapActions({
            alterTable: 'query/alterTable',
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
            this.SET_CURR_EDITOR_MODE_MAP({
                id: this.active_wke_id,
                mode: this.SQL_EDITOR_MODES.TXT_EDITOR,
            })
        },
        revertChanges() {
            this.formData = this.$help.lodash.cloneDeep(this.initialData)
        },
        handleAddComma(isLast) {
            return isLast ? '' : ', '
        },
        buildTblOptSql({ sql, dbName }) {
            //TODO: replace objectDiff with deep-diff
            const { escapeIdentifiers: escape, objectDiff } = this.$help
            //Diff of table_opts_data
            const diff = objectDiff({
                base: this.initialData.table_opts_data,
                object: this.formData.table_opts_data,
            })
            const keys = Object.keys(diff)
            const lastIdx = keys.length - 1
            keys.forEach((key, i) => {
                let isLast = i === lastIdx
                switch (key) {
                    case 'table_name':
                        sql += `RENAME TO ${escape(dbName)}.${escape(diff[key])}`
                        break
                    case 'table_engine':
                        sql += `ENGINE = ${diff[key]}`
                        break
                    case 'table_charset':
                        sql += `CHARACTER SET = ${diff[key]}`
                        break
                    case 'table_collation':
                        sql += `COLLATE = ${diff[key]}`
                        break
                    case 'table_comment':
                        sql += `COMMENT = '${diff[key]}'`
                        break
                }
                sql += this.handleAddComma(isLast)
            })
            return sql
        },
        buildDropColSql({ removedCols, sql }) {
            const { escapeIdentifiers: escape } = this.$help
            const lastIdx = removedCols.length - 1
            removedCols.forEach((row, i) => {
                let isLast = i === lastIdx
                sql += `DROP COLUMN ${escape(row.column_name)}`
                sql += this.handleAddComma(isLast)
            })
            return sql
        },
        buildColsOptsSql(sql) {
            const { arrOfObjsDiff, getObjectRows } = this.$help
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
            if (removedCols.length) sql = this.buildDropColSql({ removedCols, sql })
            //TODO: handle diff.added and diff.updated
            return sql
        },
        applyChanges() {
            const { escapeIdentifiers: escape } = this.$help
            const { dbName, table_name: initialTblName } = this.initialData.table_opts_data
            let sql = `ALTER TABLE ${escape(dbName)}.${escape(initialTblName)}\n`
            sql = this.buildTblOptSql({ sql, dbName })
            sql = this.buildColsOptsSql(sql)
            this.sql = `${sql};`
            // before opening dialog, manually clear isErrDialogShown so that query-editor can be shown
            this.isErrDialogShown = false
            this.$refs.confirmAlterDialog.open()
        },
        async confirmAlter() {
            await this.alterTable(this.sql)
            if (!this.isAlterFailed)
                this.UPDATE_TBL_CREATION_INFO_MAP({
                    id: this.active_wke_id,
                    payload: {
                        data: this.$help.lodash.cloneDeep(this.formData),
                    },
                })
            else this.isErrDialogShown = true
        },
        clearAlterResult() {
            this.UPDATE_ALTERING_TABLE_RESULT_MAP({
                id: this.active_wke_id,
            })
        },
    },
}
</script>
<style lang="scss" scoped>
.alter-err-tbl {
    background-color: $reflection;
    td {
        color: $code-color;
        vertical-align: top;
        padding-bottom: 4px;
        &:first-of-type {
            padding-right: 16px;
        }
    }
}
</style>
