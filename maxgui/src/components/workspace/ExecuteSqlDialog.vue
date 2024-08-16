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
import sqlSplitter from '@/utils/sqlSplitter'
import { QUERY_TAB_TYPE_MAP } from '@/constants/workspace'
import workspace from '@/composables/workspace'

const store = useStore()
const { t } = useI18n()
const typy = useTypy()

const activeQueryTabId = computed(() => QueryEditor.getters('activeQueryTabId'))
const activeQueryTab = computed(() => QueryTab.find(activeQueryTabId.value) || {})
const completionItems = workspace.useCompletionItems({
  queryEditorId: QueryEditor.getters('activeId'),
  queryTabId: activeQueryTabId.value,
})
const exec_sql_dlg = computed(() => store.state.workspace.exec_sql_dlg)
const hasError = computed(() => !typy(exec_sql_dlg.value, 'error').isNull)
const resErr = computed(() => typy(exec_sql_dlg.value, 'error').safeObjectOrEmpty)
const isSqlEditor = computed(() => activeQueryTab.value.type === QUERY_TAB_TYPE_MAP.SQL_EDITOR)
const isConfDlgOpened = computed({
  get: () => exec_sql_dlg.value.is_opened,
  set: (v) => store.commit('workspace/SET_EXEC_SQL_DLG', { ...exec_sql_dlg.value, is_opened: v }),
})
const currSql = computed({
  get: () => exec_sql_dlg.value.sql,
  set: (v) => store.commit('workspace/SET_EXEC_SQL_DLG', { ...exec_sql_dlg.value, sql: v }),
})
const statements = computed(() => {
  const [e, stmts] = sqlSplitter(currSql.value)
  if (e) return []
  return stmts
})
const count = computed(() => (statements.value.length > 1 ? 2 : 1))
const title = computed(() =>
  hasError.value
    ? t('errors.failedToExeStatements', count.value)
    : t('confirmations.exeStatements', count.value)
)

async function confirmExe() {
  await typy(exec_sql_dlg.value, 'on_exec').safeFunction()
}

function afterCancel() {
  typy(exec_sql_dlg.value, 'after_cancel').safeFunction()
  store.commit('workspace/SET_EXEC_SQL_DLG', { ...exec_sql_dlg.value, error: null, sql: '' })
}
</script>

<template>
  <BaseDlg
    v-model="isConfDlgOpened"
    :title="title"
    saveText="execute"
    minBodyWidth="768px"
    :hasSavingErr="hasError"
    :allowEnterToSubmit="false"
    :onSave="confirmExe"
    class="execute-sql-dlg"
    @after-close="afterCancel"
    @after-cancel="afterCancel"
  >
    <template #form-body>
      <table v-if="hasError" class="tbl-code pa-4">
        <tr>
          <td><b>sql</b></td>
          <td>{{ currSql }}</td>
        </tr>
        <tr v-for="(v, key) in resErr" :key="key">
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
        {{ hasError ? '' : $t('info.exeStatementsInfo', count) }}
      </small>
    </template>
  </BaseDlg>
</template>
