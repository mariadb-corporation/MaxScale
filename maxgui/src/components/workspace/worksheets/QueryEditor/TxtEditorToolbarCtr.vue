<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import TxtEditor from '@wsModels/TxtEditor'
import QueryResult from '@wsModels/QueryResult'
import DisableTabMovesFocusBtn from '@wkeComps/QueryEditor/DisableTabMovesFocusBtn.vue'
import RowLimit from '@wkeComps/QueryEditor/RowLimit.vue'
import FileBtnsCtr from '@wkeComps/QueryEditor/FileBtnsCtr.vue'
import prefAndStorageService from '@wsServices/prefAndStorageService'
import queryResultService from '@wsServices/queryResultService'
import { SNACKBAR_TYPE_MAP } from '@/constants'
import {
  QUERY_MODE_MAP,
  OS_CMD,
  KEYBOARD_SHORTCUT_MAP,
  COMPOUND_STMT_TYPE,
} from '@/constants/workspace'
import { getStatementClasses, enforceLimitOffset, genStatement } from '@/utils/sqlLimiter'
import sqlSplitter from '@/utils/sqlSplitter'
import workspace from '@/composables/workspace'

const props = defineProps({
  height: { type: Number, required: true },
  queryTab: { type: Object, required: true },
  queryTabTmp: { type: Object, required: true },
  queryTabConn: { type: Object, required: true },
  sql: { type: String, required: true },
  selectedSql: { type: String, required: true },
  isVisSidebarShown: { type: Boolean, required: true },
})

const {
  CTRL_D,
  META_D,
  CTRL_ENTER,
  META_ENTER,
  CTRL_SHIFT_ENTER,
  META_SHIFT_ENTER,
  CTRL_SHIFT_C,
  META_SHIFT_C,
  CTRL_M,
  META_M,
} = KEYBOARD_SHORTCUT_MAP

const store = useStore()
const { t } = useI18n()
const { getCurrentTimeStamp } = useHelpers()
const typy = useTypy()
const { validateRequiredStr } = useValidationRule()
workspace.useShortKeyListener({ handler: shortKeyHandler })

const rules = {
  snippetName: [
    validateRequiredStr,
    (v) => !query_snippets.value.map((q) => q.name).includes(v) || t('errors.duplicatedValue'),
  ],
}

const executionStatements = ref([])
const dontShowConfirm = ref(false)
const confDlg = ref({
  isOpened: false,
  title: t('confirmations.runQuery'),
  type: 'run',
  sql: '',
  isCreatingSnippet: false,
  onSave: () => null,
})
const snippet = ref({ date: '', name: '' })

const query_row_limit = computed(() => store.state.prefAndStorage.query_row_limit)
const query_confirm_flag = computed(() => store.state.prefAndStorage.query_confirm_flag)
const query_snippets = computed(() => store.state.prefAndStorage.query_snippets)

const tab_moves_focus = computed(() => store.state.prefAndStorage.tab_moves_focus)
const max_statements = computed(() => store.state.prefAndStorage.max_statements)

const rowLimit = computed({
  get: () => query_row_limit.value,
  set: (v) => store.commit('prefAndStorage/SET_QUERY_ROW_LIMIT', v),
})

const isExecuting = computed(() => typy(props.queryTabTmp, 'query_results.is_loading').safeBoolean)
const hasKillFlag = computed(() => typy(props.queryTabTmp, 'has_kill_flag').safeBoolean)
const isQueryTabConnBusy = computed(() => typy(props.queryTabConn, 'is_busy').safeBoolean)
const isRunBtnDisabled = computed(
  () => !props.sql || !props.queryTabConn.id || isQueryTabConnBusy.value || isExecuting.value
)
const isVisBtnDisabled = computed(
  () => !props.queryTabConn.id || (isQueryTabConnBusy.value && isExecuting.value)
)
const executionSQL = computed(() => executionStatements.value.map((s) => s.text).join(';\n\n'))

function toggleVisSidebar() {
  TxtEditor.update({
    where: props.queryTab.id,
    data(obj) {
      obj.is_vis_sidebar_shown = !obj.is_vis_sidebar_shown
    },
  })
}

function hasQueryText(mode) {
  return (mode === 'selected' && props.selectedSql) || (mode === 'all' && props.sql)
}

function handleEnforceLimitOffset(statementClasses) {
  const errors = [],
    statements = []
  for (const statementClass of statementClasses) {
    const [e, statement] = enforceLimitOffset({ statementClass, limit: query_row_limit.value })
    if (e !== undefined) errors.push(e)
    if (statement) statements.push(statement)
  }
  return [errors, statements]
}

function processSQL(sql) {
  const [e, stmts] = sqlSplitter(sql)
  let processedStatements = []
  if (e)
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
      text: [t('errors.splitStatements')],
      type: SNACKBAR_TYPE_MAP.ERROR,
    })
  else
    stmts.forEach((stmt) => {
      if (stmt.delimiter === ';') {
        const [e, statementClasses] = getStatementClasses(stmt.text)
        if (!e) {
          const [errors, statements] = handleEnforceLimitOffset(statementClasses)
          if (errors.length)
            store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
              text: [`${t('errors.injectLimit')}:`, ...errors.map((err) => `${err.message}.`)],
              type: SNACKBAR_TYPE_MAP.ERROR,
            })
          else processedStatements.push(...statements)
        }
      } else processedStatements.push(genStatement({ text: stmt.text, type: COMPOUND_STMT_TYPE }))
    })
  executionStatements.value = processedStatements
}

async function handleRun(mode) {
  if (!isRunBtnDisabled.value && hasQueryText(mode)) {
    processSQL(mode === 'selected' ? props.selectedSql : props.sql)
    if (executionStatements.value.length > max_statements.value)
      store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
        text: [t('errors.maxStatements', [max_statements.value])],
        type: SNACKBAR_TYPE_MAP.ERROR,
      })
    else if (query_confirm_flag.value) {
      dontShowConfirm.value = false // reset checkbox state
      confDlg.value = {
        isOpened: true,
        title: t('confirmations.runQuery'),
        type: 'run',
        isCreatingSnippet: false,
        sql: executionSQL.value,
        onSave: async () => {
          if (dontShowConfirm.value) store.commit('prefAndStorage/SET_QUERY_CONFIRM_FLAG', 0)
          await runSQL()
        },
      }
    } else await runSQL()
  }
}

async function runSQL() {
  QueryResult.update({ where: props.queryTab.id, data: { query_mode: QUERY_MODE_MAP.QUERY_VIEW } })
  await queryResultService.exeStatements(executionStatements.value)
}

function openSnippetDlg() {
  if (props.sql) {
    snippet.value.date = getCurrentTimeStamp()
    snippet.value.name = ''
    confDlg.value = {
      ...confDlg.value,
      isOpened: true,
      title: t('confirmations.createSnippet'),
      type: 'create',
      isCreatingSnippet: true,
      sql: props.selectedSql || props.sql,
      onSave: addSnippet,
    }
  }
}

function addSnippet() {
  const payload = {
    sql: props.sql,
    ...snippet.value,
  }
  if (props.selectedSql) payload.sql = props.selectedSql
  prefAndStorageService.saveQuerySnippet(payload)
}

async function shortKeyHandler(key) {
  switch (key) {
    case CTRL_D:
    case META_D:
      openSnippetDlg()
      break
    case CTRL_ENTER:
    case META_ENTER:
      handleRun('selected')
      break
    case CTRL_SHIFT_ENTER:
    case META_SHIFT_ENTER:
      handleRun('all')
      break
    case CTRL_SHIFT_C:
    case META_SHIFT_C:
      if (isExecuting.value) await queryResultService.killQuery()
      break
    case CTRL_M:
    case META_M:
      store.commit('prefAndStorage/SET_TAB_MOVES_FOCUS', !tab_moves_focus.value)
      break
  }
}
</script>

<template>
  <div
    class="txt-editor-toolbar border-bottom--table-border d-flex align-center"
    :style="{ height: `${height}px` }"
  >
    <TooltipBtn
      v-if="isExecuting"
      square
      size="small"
      variant="text"
      color="primary"
      :disabled="hasKillFlag"
      data-test="stop-btn"
      @click="queryResultService.killQuery"
    >
      <template #btn-content>
        <VIcon size="16" icon="mxs:stopped" />
      </template>
      {{ $t('stopStatements', 2) }}
      <br />
      {{ OS_CMD }} + SHIFT + C
    </TooltipBtn>
    <TooltipBtn
      v-else
      square
      size="small"
      variant="text"
      color="primary"
      :disabled="isRunBtnDisabled"
      data-test="run-btn"
      @click="handleRun(selectedSql ? 'selected' : 'all')"
    >
      <template #btn-content>
        <VIcon size="16" icon="mxs:running" />
      </template>
      {{ $t('runStatements', { quantity: selectedSql ? $t('selected') : $t('all') }) }}
      <br />
      {{ OS_CMD }} {{ selectedSql ? '' : '+ SHIFT' }} + ENTER
    </TooltipBtn>
    <TooltipBtn
      square
      size="small"
      :variant="isVisSidebarShown ? 'flat' : 'text'"
      color="primary"
      :disabled="isVisBtnDisabled"
      data-test="visualize-btn"
      @click="toggleVisSidebar"
    >
      <template #btn-content>
        <VIcon size="16" icon="mxs:reports" />
      </template>
      {{ $t('visualizedConfig', { action: isVisSidebarShown ? $t('hide') : $t('show') }) }}
    </TooltipBtn>
    <TooltipBtn
      square
      size="small"
      variant="text"
      color="primary"
      :disabled="!sql"
      data-test="create-snippet-btn"
      @click="openSnippetDlg"
    >
      <template #btn-content>
        <VIcon size="19" icon="$mdiStarPlusOutline" />
      </template>
      {{ $t('createQuerySnippet') }}
      <br />
      {{ OS_CMD }} + D
    </TooltipBtn>
    <FileBtnsCtr :queryTab="queryTab" />
    <VSpacer />
    <DisableTabMovesFocusBtn v-if="tab_moves_focus" class="mr-1" />
    <RowLimit
      v-model="rowLimit"
      minimized
      :prefix="$t('defLimit')"
      class="mr-3 flex-grow-0"
      hide-details
      showErrInSnackbar
    />
    <BaseDlg
      v-model="confDlg.isOpened"
      :title="confDlg.title"
      :saveText="confDlg.type"
      minBodyWidth="768px"
      :onSave="confDlg.onSave"
      closeImmediate
    >
      <template #form-body>
        <div class="mb-4 readonly-sql-code-wrapper pt-2">
          <SqlEditor
            :modelValue="confDlg.sql"
            class="readonly-editor fill-height"
            readOnly
            :options="{ fontSize: 10, contextmenu: false }"
          />
        </div>
        <template v-if="confDlg.isCreatingSnippet">
          <label class="label-field text-small-text label--required text-capitalize">
            {{ $t('prefix') }}
          </label>
          <VTextField
            v-model="snippet.name"
            type="text"
            :rules="rules.snippetName"
            density="compact"
            hide-details="auto"
          />
        </template>
      </template>
      <template v-if="!confDlg.isCreatingSnippet" #action-prepend>
        <VCheckboxBtn v-model="dontShowConfirm" :label="$t('dontAskMeAgain')" inline />
        <VSpacer />
      </template>
    </BaseDlg>
  </div>
</template>
