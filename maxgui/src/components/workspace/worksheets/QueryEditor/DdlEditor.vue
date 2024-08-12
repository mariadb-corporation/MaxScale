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
import DdlEditor from '@wsModels/DdlEditor'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import DdlEditorToolbar from '@wkeComps/QueryEditor/DdlEditorToolbar.vue'
import ResultView from '@wkeComps/QueryEditor/ResultView.vue'
import workspace from '@/composables/workspace'
import { WS_EDITOR_KEY } from '@/constants/injectionKeys'
import { COMPACT_TOOLBAR_HEIGHT, KEYBOARD_SHORTCUT_MAP, OS_CMD } from '@/constants/workspace'
import keyBindingMap from '@/components/common/SqlEditor/keyBindingMap'
import queryConnService from '@wsServices/queryConnService'

const props = defineProps({
  dim: { type: Object, required: true },
  queryEditorTmp: { type: Object, required: true },
  queryTab: { type: Object, required: true },
})

const store = useStore()
const typy = useTypy()
const { t } = useI18n()
const { pxToPct, pctToPx } = useHelpers()

const { CTRL_ENTER, CTRL_SHIFT_C, CTRL_O, CTRL_S, CTRL_SHIFT_S, CTRL_M } = KEYBOARD_SHORTCUT_MAP

const EDITOR_ACTIONS = [
  {
    label: t('exeStmt'),
    keybindings: keyBindingMap[CTRL_ENTER],
    run: () => dispatchEvt(CTRL_ENTER),
  },
  {
    label: t('stopStatements', 1),
    keybindings: keyBindingMap[CTRL_SHIFT_C],
    run: () => dispatchEvt(CTRL_SHIFT_C),
  },
  {
    label: t('openScript'),
    keybindings: keyBindingMap[CTRL_O],
    run: () => dispatchEvt(CTRL_O),
  },
  {
    label: t('saveScript'),
    keybindings: keyBindingMap[CTRL_S],
    run: () => dispatchEvt(CTRL_S),
  },
  {
    label: t('saveScriptAs'),
    keybindings: keyBindingMap[CTRL_SHIFT_S],
    run: () => dispatchEvt(CTRL_SHIFT_S),
  },
]

const queryTabId = computed(() => props.queryTab.id)
const tab_moves_focus = computed(() => store.state.prefAndStorage.tab_moves_focus)
const ddlEditor = computed(() => DdlEditor.find(queryTabId.value) || {})
const isAltering = computed(() => !typy(ddlEditor.value, 'active_node').isNull)
const queryTabTmp = computed(() => QueryTabTmp.find(queryTabId.value) || {})
const queryTabConn = computed(() => queryConnService.findQueryTabConn(queryTabId.value))
const ddl_result = computed(() => typy(queryTabTmp.value, 'ddl_result').safeObjectOrEmpty)
const showGuide = computed(() => typy(ddl_result.value).isEmptyObject)
const sql = computed({
  get: () => typy(ddlEditor.value, 'sql').safeString,
  set: (v) => DdlEditor.update({ where: queryTabId.value, data: { sql: v } }),
})
const editorHeight = computed(() => props.dim.height - COMPACT_TOOLBAR_HEIGHT)
const resultDim = computed(() => ({
  width: props.dim.width,
  height: pctToPx({ pct: 100 - sqlEditorPaneHeightPct.value, containerPx: editorHeight.value }),
}))
const sqlEditorPaneMinPct = computed(() =>
  pxToPct({ px: showGuide.value ? 38 : 24, containerPx: editorHeight.value })
)

const dispatchEvt = useEventDispatcher(WS_EDITOR_KEY)
const completionItems = workspace.useCompletionItems({
  queryEditorId: typy(props.queryEditorTmp, 'id').safeString,
  queryTabId: queryTabId.value,
})
const sqlEditorPaneHeightPct = ref(
  pxToPct({ px: editorHeight.value - 120, containerPx: editorHeight.value })
)
</script>

<template>
  <div class="d-flex flex-column fill-height">
    <DdlEditorToolbar
      :height="COMPACT_TOOLBAR_HEIGHT"
      :queryTab="queryTab"
      :queryTabTmp="queryTabTmp"
      :queryTabConn="queryTabConn"
      :ddlEditor="ddlEditor"
      :isAltering="isAltering"
      :sql="sql"
    />
    <ResizablePanels
      v-model="sqlEditorPaneHeightPct"
      :boundary="editorHeight"
      split="horiz"
      :minPercent="sqlEditorPaneMinPct"
      :maxPercent="100 - sqlEditorPaneMinPct"
    >
      <template #pane-left>
        <SqlEditor
          v-model="sql"
          :isTabMoveFocus="tab_moves_focus"
          class="editor pt-2 fill-height"
          :completionItems="completionItems"
          isKeptAlive
          :customActions="EDITOR_ACTIONS"
          @toggle-tab-focus-mode="dispatchEvt(CTRL_M)"
        />
      </template>
      <template #pane-right>
        <div class="border-top--table-border text-body-2 text-small-text fill-height pt-2">
          <i18n-t
            v-if="showGuide"
            keypath="ddlResultTabGuide"
            scope="global"
            tag="div"
            class="d-flex align-center flex-wrap px-5"
            data-test="ddl-result-tab-guide"
          >
            <template #shortcut>
              &nbsp;
              <b>
                <kbd>{{ OS_CMD }}</kbd> + <kbd>ENTER</kbd>
              </b>
              &nbsp;
            </template>
            <template #icon>
              <VIcon color="primary" size="16" icon="mxs:running" class="mx-1" />
            </template>
          </i18n-t>
          <ResultView
            v-else
            :data="ddl_result"
            :dim="resultDim"
            :dataTableProps="{ hideToolbar: true }"
            class="fill-height"
          />
        </div>
      </template>
    </ResizablePanels>
  </div>
</template>
