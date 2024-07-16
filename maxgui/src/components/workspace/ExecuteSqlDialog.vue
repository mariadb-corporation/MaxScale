<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import QueryEditor from '@wsModels/QueryEditor'
import QueryTab from '@wsModels/QueryTab'
import SchemaSidebar from '@wsModels/SchemaSidebar'
import { splitSQL } from '@/utils/sqlLimiter'
import { QUERY_TAB_TYPES } from '@/constants/workspace'

const store = useStore()
const { t } = useI18n()
const typy = useTypy()

const activeQueryTab = computed(() => QueryTab.find(QueryEditor.getters('activeQueryTabId')) || {})
const exec_sql_dlg = computed(() => store.state.workspace.exec_sql_dlg)
const isExecFailed = computed(() => store.getters['workspace/isExecFailed'])
const getExecErr = computed(() => store.getters['workspace/getExecErr'])
const isSqlEditor = computed(() => activeQueryTab.value.type === QUERY_TAB_TYPES.SQL_EDITOR)
const isConfDlgOpened = computed({
  get: () => exec_sql_dlg.value.is_opened,
  set: (v) => store.commit('workspace/SET_EXEC_SQL_DLG', { ...exec_sql_dlg.value, is_opened: v }),
})
const currSql = computed({
  get: () => exec_sql_dlg.value.sql,
  set: (v) => store.commit('workspace/SET_EXEC_SQL_DLG', { ...exec_sql_dlg.value, sql: v }),
})
const statements = computed(() => splitSQL(currSql.value))
const count = computed(() => (statements.value.length > 1 ? 2 : 1))
const title = computed(() =>
  isExecFailed.value
    ? t('errors.failedToExeStatements', count.value)
    : t('confirmations.exeStatements', count.value)
)
const completionItems = computed(() => SchemaSidebar.getters('activeCompletionItems'))

async function confirmExe() {
  await typy(exec_sql_dlg.value, 'on_exec').safeFunction()
}

function afterCancel() {
  typy(exec_sql_dlg.value, 'after_cancel').safeFunction()
  store.commit('workspace/SET_EXEC_SQL_DLG', { ...exec_sql_dlg.value, result: null, sql: '' })
}
</script>

<template>
  <BaseDlg
    v-model="isConfDlgOpened"
    :title="title"
    saveText="execute"
    minBodyWidth="768px"
    :hasSavingErr="isExecFailed"
    :allowEnterToSubmit="false"
    :onSave="confirmExe"
    class="execute-sql-dlg"
    @after-close="afterCancel"
    @after-cancel="afterCancel"
  >
    <template #form-body>
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
        class="mb-4 pt-2 pl-2 border--table-border"
        :style="{ height: `${exec_sql_dlg.editor_height}px` }"
      >
        <!--When there are more than one `SqlEditor` components renders on the same view,
            Completion items will be duplicated as the `registerCompletionItemProvider` registers
            items at global scope. https://github.com/microsoft/monaco-editor/issues/1957
            So when the QueryTab type is SQL_EDITOR, uses the `skipRegCompleters` props to skip
            the registration.
        -->
        <SqlEditor
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
      <p v-if="exec_sql_dlg.extra_info" class="my-4">
        {{ exec_sql_dlg.extra_info }}
      </p>
      <small data-test="small-txt">
        {{ isExecFailed ? '' : $t('info.exeStatementsInfo', count) }}
      </small>
    </template>
  </BaseDlg>
</template>
