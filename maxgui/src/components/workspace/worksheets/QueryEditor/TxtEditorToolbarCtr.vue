<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
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
import TxtEditor from '@wsModels/TxtEditor'
import QueryResult from '@wsModels/QueryResult'
import RowLimit from '@wkeComps/QueryEditor/RowLimit.vue'
import FileBtnsCtr from '@wkeComps/QueryEditor/FileBtnsCtr.vue'
import prefAndStorageService from '@wsServices/prefAndStorageService'
import queryResultService from '@wsServices/queryResultService'
import {
  WS_EMITTER_KEY,
  EDITOR_EMITTER_KEY,
  QUERY_MODES,
  OS_KEY,
  IS_MAC_OS,
} from '@/constants/workspace'
import { splitQuery } from '@/utils/queryUtils'

const props = defineProps({
  height: { type: Number, required: true },
  queryTab: { type: Object, required: true },
  queryTabTmp: { type: Object, required: true },
  queryTabConn: { type: Object, required: true },
  queryTxt: { type: String, required: true },
  selectedQueryTxt: { type: String, required: true },
  isVisSidebarShown: { type: Boolean, required: true },
})
const store = useStore()
const { t } = useI18n()
const typy = useTypy()
const wsEventListener = inject(WS_EMITTER_KEY)
const editorEventListener = inject(EDITOR_EMITTER_KEY)

const rules = { snippetName: [(v) => validateSnippetName(v)] }

const dontShowConfirm = ref(false)
const activeRunMode = ref('all')
const confDlg = ref({
  isOpened: false,
  title: t('confirmations.runQuery'),
  type: 'run',
  sqlTxt: '',
  isCreatingSnippet: false,
  onSave: () => null,
})
const snippet = ref({ date: '', name: '' })
const rowLimitValidity = ref(true)

const query_confirm_flag = computed(() => store.state.prefAndStorage.query_confirm_flag)
const query_snippets = computed(() => store.state.prefAndStorage.query_snippets)

const tab_moves_focus = computed(() => store.state.prefAndStorage.tab_moves_focus)
const max_statements = computed(() => store.state.prefAndStorage.max_statements)

const rowLimit = computed({
  get: () => store.state.prefAndStorage.query_row_limit,
  set: (v) => store.commit('prefAndStorage/SET_QUERY_ROW_LIMIT', v),
})

const isExecuting = computed(() => typy(props.queryTabTmp, 'query_results.is_loading').safeBoolean)
const hasKillFlag = computed(() => typy(props.queryTabTmp, 'has_kill_flag').safeBoolean)
const isQueryTabConnBusy = computed(() => typy(props.queryTabConn, 'is_busy').safeBoolean)
const isRunBtnDisabled = computed(
  () => !props.queryTxt || !props.queryTabConn.id || isQueryTabConnBusy.value || isExecuting.value
)
const isVisBtnDisabled = computed(
  () => !props.queryTabConn.id || (isQueryTabConnBusy.value && isExecuting.value)
)
const sqlTxt = computed(() =>
  activeRunMode.value === 'selected' ? props.selectedQueryTxt : props.queryTxt
)
let unwatch_wsEventListener, unwatch_editorKeypress

onActivated(() => {
  unwatch_wsEventListener = watch(wsEventListener, (v) => shortKeyHandler(v.event))
  unwatch_editorKeypress = watch(editorEventListener, (v) => shortKeyHandler(v.event))
})

onDeactivated(() => cleanUp())
onBeforeUnmount(() => cleanUp())

function cleanUp() {
  unwatch_wsEventListener()
  unwatch_editorKeypress()
}

function toggleVisSidebar() {
  TxtEditor.update({
    where: props.queryTab.id,
    data(obj) {
      obj.is_vis_sidebar_shown = !obj.is_vis_sidebar_shown
    },
  })
}

/**
 * Only open dialog when its corresponding query text exists
 */
function shouldOpenDialog(mode) {
  return (mode === 'selected' && props.selectedQueryTxt) || (mode === 'all' && props.queryTxt)
}

async function handleRun(mode) {
  if (!isRunBtnDisabled.value)
    if (splitQuery(sqlTxt.value).length > max_statements.value)
      store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
        text: [t('errors.maxStatements', [max_statements.value])],
        type: 'error',
      })
    else {
      if (!query_confirm_flag.value) await onRun(mode)
      else if (shouldOpenDialog(mode)) {
        activeRunMode.value = mode
        dontShowConfirm.value = false // clear checkbox state
        confDlg.value = {
          ...confDlg.value,
          isOpened: true,
          title: t('confirmations.runQuery'),
          type: 'run',
          isCreatingSnippet: false,
          sqlTxt: sqlTxt.value,
          onSave: confirmRunning,
        }
      }
    }
}

async function confirmRunning() {
  if (dontShowConfirm.value) store.commit('prefAndStorage/SET_QUERY_CONFIRM_FLAG', 0)
  await onRun(activeRunMode.value)
}

/**
 * @param {String} mode Mode to execute query: All or selected
 */
async function onRun(mode) {
  QueryResult.update({
    where: props.queryTab.id,
    data: {
      query_mode: QUERY_MODES.QUERY_VIEW,
    },
  })
  switch (mode) {
    case 'all':
      if (props.queryTxt) await queryResultService.executeSQL(props.queryTxt)
      break
    case 'selected':
      if (props.selectedQueryTxt) await queryResultService.executeSQL(props.selectedQueryTxt)
      break
  }
}

function openSnippetDlg() {
  if (props.queryTxt) {
    snippet.value.date = new Date().valueOf()
    snippet.value.name = ''
    confDlg.value = {
      ...confDlg.value,
      isOpened: true,
      title: t('confirmations.createSnippet'),
      type: 'create',
      isCreatingSnippet: true,
      sqlTxt: props.selectedQueryTxt ? props.selectedQueryTxt : props.queryTxt,
      onSave: addSnippet,
    }
  }
}

function addSnippet() {
  let payload = {
    sql: props.queryTxt,
    ...snippet.value,
  }
  if (props.selectedQueryTxt) payload.sql = props.selectedQueryTxt
  prefAndStorageService.saveQuerySnippet(payload)
}

function validateSnippetName(v) {
  const names = query_snippets.value.map((q) => q.name)
  if (!v) return t('errors.requiredInput', { inputName: t('prefix') })
  else if (names.includes(v)) return t('errors.duplicatedValue')
  return true
}

async function shortKeyHandler(key) {
  switch (key) {
    case 'ctrl-d':
    case 'mac-cmd-d':
      openSnippetDlg()
      break
    case 'ctrl-enter':
    case 'mac-cmd-enter':
      handleRun('selected')
      break
    case 'ctrl-shift-enter':
    case 'mac-cmd-shift-enter':
      handleRun('all')
      break
    case 'ctrl-shift-c':
    case 'mac-cmd-shift-c':
      if (isExecuting.value) await queryResultService.killQuery()
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
      class="toolbar-square-btn"
      variant="text"
      color="primary"
      :disabled="hasKillFlag"
      data-test="stop-btn"
      @click="queryResultService.killQuery"
    >
      <template #btn-content>
        <VIcon size="16" icon="mxs:stopped" />
      </template>
      {{ $t('stopStatements') }}
      <br />
      {{ OS_KEY }} + SHIFT + C
    </TooltipBtn>
    <TooltipBtn
      v-else
      class="toolbar-square-btn"
      variant="text"
      color="primary"
      :disabled="isRunBtnDisabled"
      data-test="run-btn"
      @click="handleRun(selectedQueryTxt ? 'selected' : 'all')"
    >
      <template #btn-content>
        <VIcon size="16" icon="mxs:running" />
      </template>
      {{ $t('runStatements', { quantity: selectedQueryTxt ? $t('selected') : $t('all') }) }}
      <br />
      {{ OS_KEY }} {{ selectedQueryTxt ? '' : '+ SHIFT' }} + ENTER
    </TooltipBtn>
    <TooltipBtn
      class="toolbar-square-btn"
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
      class="toolbar-square-btn"
      variant="text"
      color="primary"
      :disabled="!queryTxt"
      data-test="create-snippet-btn"
      @click="openSnippetDlg"
    >
      <template #btn-content>
        <VIcon size="19" icon="$mdiStarPlusOutline" />
      </template>
      {{ $t('createQuerySnippet') }}
      <br />
      {{ OS_KEY }} + D
    </TooltipBtn>
    <FileBtnsCtr :queryTab="queryTab" />
    <VSpacer />
    <TooltipBtn
      v-if="tab_moves_focus"
      class="disable-tab-move-focus-mode-btn mr-1 text-capitalize"
      variant="text"
      color="primary"
      size="small"
      density="comfortable"
      @click="$emit('disable-tab-move-focus')"
    >
      <template #btn-content>
        {{ $t('tabMovesFocus') }}
      </template>
      {{ $t('disableAccessibilityMode') }}
      <br />
      {{ OS_KEY }} {{ IS_MAC_OS ? '+ SHIFT' : '' }} + M
    </TooltipBtn>
    <VForm v-model="rowLimitValidity" class="fill-height d-flex align-center mr-3">
      <RowLimit
        v-model="rowLimit"
        class="row-limit"
        density="compact"
        :prefix="$t('rowLimit')"
        hide-details
      />
    </VForm>
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
            :modelValue="confDlg.sqlTxt"
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
            class="vmb-2"
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

<style lang="scss" scoped>
.row-limit {
  width: 200px;
  :deep(.v-field__outline) {
    .v-field__outline__start,
    .v-field__outline__end {
      border-radius: 0;
    }
  }
}
</style>
