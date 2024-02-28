<template>
    <mxs-dlg
        v-model="isConfDlgOpened"
        :title="title"
        saveText="execute"
        minBodyWidth="768px"
        :hasSavingErr="isExecFailed"
        :allowEnterToSubmit="false"
        :onSave="$typy(exec_sql_dlg, 'on_exec').safeFunction"
        @after-close="$typy(exec_sql_dlg, 'after_cancel').safeFunction()"
        @after-cancel="$typy(exec_sql_dlg, 'after_cancel').safeFunction()"
    >
        <template v-slot:form-body>
            <table v-if="isExecFailed" class="tbl-code pa-4">
                <tr>
                    <td><b>sql</b></td>
                    <td>{{ currSql }}</td>
                </tr>
                <tr v-for="(v, key) in getExecErr" :key="key">
                    <td>
                        <b>{{ key }}</b>
                    </td>
                    <td>{{ v }}</td>
                </tr>
            </table>

            <div
                v-else
                class="mb-4 pt-2 pl-2 mxs-color-helper all-border-table-border"
                :style="{ height: `${exec_sql_dlg.editor_height}px` }"
            >
                <!--When there are more than one `mxs-sql-editor` components renders on the same view,
                    Completion items will be duplicated as the `registerCompletionItemProvider` registers
                    items at global scope. https://github.com/microsoft/monaco-editor/issues/1957
                    So when the QueryTab type is SQL_EDITOR, uses the `skipRegCompleters` props to skip
                    the registration.
                -->
                <mxs-sql-editor
                    v-if="isConfDlgOpened"
                    v-model="currSql"
                    class="fill-height"
                    :completionItems="completionItems"
                    :options="{
                        fontSize: 10,
                        contextmenu: false,
                        wordWrap: 'on',
                    }"
                    :skipRegCompleters="isSqlEditor"
                />
            </div>

            <small>
                {{ isExecFailed ? '' : $mxs_tc('info.exeStatementsInfo', stmtI18nPluralization) }}
            </small>
        </template>
    </mxs-dlg>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapMutations, mapState, mapGetters } from 'vuex'
import QueryTab from '@wsModels/QueryTab'
import SchemaSidebar from '@wsSrc/store/orm/models/SchemaSidebar'

export default {
    name: 'execute-sql-dialog',
    computed: {
        ...mapState({
            exec_sql_dlg: state => state.mxsWorkspace.exec_sql_dlg,
        }),
        ...mapGetters({
            isExecFailed: 'mxsWorkspace/isExecFailed',
            getExecErr: 'mxsWorkspace/getExecErr',
        }),
        isSqlEditor() {
            return QueryTab.getters('isSqlEditor')
        },
        isConfDlgOpened: {
            get() {
                return this.exec_sql_dlg.is_opened
            },
            set(v) {
                this.SET_EXEC_SQL_DLG({ ...this.exec_sql_dlg, is_opened: v })
            },
        },
        currSql: {
            get() {
                return this.exec_sql_dlg.sql
            },
            set(v) {
                this.SET_EXEC_SQL_DLG({ ...this.exec_sql_dlg, sql: v })
            },
        },
        title() {
            return this.isExecFailed
                ? this.$mxs_tc('errors.failedToExeStatements', this.stmtI18nPluralization)
                : this.$mxs_tc('confirmations.exeStatements', this.stmtI18nPluralization)
        },
        stmtI18nPluralization() {
            const statementCounts = (this.currSql.match(/;/g) || []).length
            return statementCounts > 1 ? 2 : 1
        },
        completionItems() {
            return SchemaSidebar.getters('activeCompletionItems')
        },
    },
    methods: {
        ...mapMutations({
            SET_EXEC_SQL_DLG: 'mxsWorkspace/SET_EXEC_SQL_DLG',
        }),
    },
}
</script>
