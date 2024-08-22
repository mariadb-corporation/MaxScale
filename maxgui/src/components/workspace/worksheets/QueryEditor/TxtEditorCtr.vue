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
import QueryTabTmp from '@wsModels/QueryTabTmp'
import TxtEditorToolbarCtr from '@wkeComps/QueryEditor/TxtEditorToolbarCtr.vue'
import ChartConfig from '@wkeComps/QueryEditor/ChartConfig.vue'
import ChartPane from '@wkeComps/QueryEditor/ChartPane.vue'
import QueryResultCtr from '@wkeComps/QueryEditor/QueryResultCtr.vue'
import queryConnService from '@wsServices/queryConnService'
import workspace from '@/composables/workspace'
import {
  QUERY_MODE_MAP,
  CHART_TYPE_MAP,
  CHART_AXIS_TYPE_MAP,
  COMPACT_TOOLBAR_HEIGHT,
  KEYBOARD_SHORTCUT_MAP,
} from '@/constants/workspace'
import { WS_EDITOR_KEY } from '@/constants/injectionKeys'
import keyBindingMap from '@/components/common/SqlEditor/keyBindingMap'

const props = defineProps({
  dim: { type: Object, required: true },
  queryEditorTmp: { type: Object, required: true },
  queryTab: { type: Object, required: true },
})

const { CTRL_D, CTRL_ENTER, CTRL_SHIFT_ENTER, CTRL_SHIFT_C, CTRL_O, CTRL_S, CTRL_SHIFT_S, CTRL_M } =
  KEYBOARD_SHORTCUT_MAP

const store = useStore()
const { t } = useI18n()
const typy = useTypy()
const { pxToPct } = useHelpers()

const dispatchEvt = useEventDispatcher(WS_EDITOR_KEY)

const EDITOR_ACTIONS = [
  {
    label: t('runStatements', { quantity: t('selected') }),
    keybindings: keyBindingMap[CTRL_ENTER],
    run: () => dispatchEvt(CTRL_ENTER),
  },
  {
    label: t('runStatements', { quantity: t('all') }),
    keybindings: keyBindingMap[CTRL_SHIFT_ENTER],
    run: () => dispatchEvt(CTRL_SHIFT_ENTER),
  },
  {
    label: t('stopStatements', 2),
    keybindings: keyBindingMap[CTRL_SHIFT_C],
    run: () => dispatchEvt(CTRL_SHIFT_C),
  },
  {
    label: t('createQuerySnippet'),
    keybindings: keyBindingMap[CTRL_D],
    run: () => dispatchEvt(CTRL_D),
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

const VIS_SIDEBAR_WIDTH = 250

const editorPanePctWidth = ref(100)
const editorRef = ref(null)
const chartOpt = ref({})
const selectedSql = ref('')

const { placeToEditor, draggingTxt, dropTxtToEditor } = workspace.useSqlEditorDragDrop(editorRef)

const queryTabId = computed(() => props.queryTab.id)
const completionItems = workspace.useCompletionItems({
  queryEditorId: typy(props.queryEditorTmp, 'id').safeString,
  queryTabId: queryTabId.value,
})

const query_pane_pct_height = computed(() => store.state.prefAndStorage.query_pane_pct_height)
const tab_moves_focus = computed(() => store.state.prefAndStorage.tab_moves_focus)

const queryTabConn = computed(() => queryConnService.findQueryTabConn(queryTabId.value))
const queryTabTmp = computed(() => QueryTabTmp.find(queryTabId.value) || {})
const prvwDataResultSets = computed(() => {
  const resSets = []
  const { prvw_data, prvw_data_details, active_node } = queryTabTmp.value
  const nodeQualifiedName = typy(active_node, 'qualified_name').safeString
  const addToResSets = (data, mode) => {
    if (!typy(data).isEmptyObject)
      resSets.push({ id: `${t(mode)} of ${nodeQualifiedName}`, ...data })
  }
  addToResSets(prvw_data, 'previewData')
  addToResSets(prvw_data_details, 'viewDetails')
  return resSets
})
const queryResultSets = computed(() => {
  let count = 0
  return typy(queryTabTmp.value, 'query_results.data').safeArray.reduce(
    (resultSets, stmtResults) => {
      typy(stmtResults, 'data.attributes.results').safeArray.forEach((result) => {
        if (result.data) {
          ++count
          resultSets.push({ id: `RESULT SET ${count}`, ...result })
        }
      })
      return resultSets
    },
    []
  )
})
const resultSets = computed(() => [...queryResultSets.value, ...prvwDataResultSets.value])

const showVisChart = computed(() =>
  Boolean(
    typy(chartOpt.value, 'type').safeString &&
      typy(chartOpt.value, 'chartData.labels').safeArray.length
  )
)
const isChartMaximized = computed(() => typy(chartOpt.value, 'isMaximized').safeBoolean)
const panesDim = computed(() => ({
  width: props.dim.width,
  height: props.dim.height - COMPACT_TOOLBAR_HEIGHT,
}))
const chartContainerHeight = computed(
  () => (panesDim.value.height * queryPanePctHeight.value) / 100
)
const visSidebarPct = computed(() =>
  pxToPct({ px: VIS_SIDEBAR_WIDTH, containerPx: panesDim.value.width })
)
const mainPanePct = computed(() => (isVisSidebarShown.value ? 100 - visSidebarPct.value : 100))
const queryPaneMinPctHeight = computed(() =>
  pxToPct({ px: 24, containerPx: panesDim.value.height })
)
const queryPaneMaxPctHeight = computed(() => 100 - queryPaneMinPctHeight.value)
const queryPanePctHeight = computed({
  get: () => query_pane_pct_height.value,
  set: (v) => store.commit('prefAndStorage/SET_QUERY_PANE_PCT_HEIGHT', v),
})
const editorPaneMinPctWidth = computed(() =>
  showVisChart.value ? pxToPct({ px: 32, containerPx: panesDim.value.width }) : 0
)
const txtEditor = computed(() => TxtEditor.find(queryTabId.value) || {})
const sql = computed({
  get: () => typy(txtEditor.value, 'sql').safeString,
  set: (v) => TxtEditor.update({ where: queryTabId.value, data: { sql: v } }),
})
const resultPaneDim = computed(() => ({
  width: panesDim.value.width - (isVisSidebarShown.value ? VIS_SIDEBAR_WIDTH : 0),
  height: (panesDim.value.height * (100 - queryPanePctHeight.value)) / 100,
}))
const isVisSidebarShown = computed(() => typy(txtEditor.value, 'is_vis_sidebar_shown').safeBoolean)

watch(isChartMaximized, (v) => {
  editorPanePctWidth.value = v ? editorPaneMinPctWidth.value : 50
})
watch(showVisChart, (v) => {
  editorPanePctWidth.value = v ? 50 : 100
})

onMounted(() => setDefChartOptState())

function getDefChartOpt() {
  return {
    type: '',
    chartData: { datasets: [], labels: [] },
    axisKeys: { x: '', y: '' },
    axesType: { x: '', y: '' },
    tableData: [],
    hasTrendline: false,
    isHorizChart: false,
    isMaximized: false,
  }
}
function setDefChartOptState() {
  chartOpt.value = getDefChartOpt()
}

function onSelectText(v) {
  selectedSql.value = v
}

defineExpose({ placeToEditor, draggingTxt, dropTxtToEditor })
</script>

<template>
  <div class="d-flex flex-column fill-height">
    <TxtEditorToolbarCtr
      class="d-flex"
      :height="COMPACT_TOOLBAR_HEIGHT"
      :queryTab="queryTab"
      :queryTabTmp="queryTabTmp"
      :queryTabConn="queryTabConn"
      :sql="sql"
      :selectedSql="selectedSql"
      :isVisSidebarShown="isVisSidebarShown"
    />
    <ResizablePanels
      :modelValue="mainPanePct"
      :boundary="panesDim.width"
      class="main-pane__content d-flex"
      split="vert"
      disable
    >
      <template #pane-left>
        <ResizablePanels
          v-model="queryPanePctHeight"
          :boundary="panesDim.height"
          split="horiz"
          :minPercent="queryPaneMinPctHeight"
          :maxPercent="queryPaneMaxPctHeight"
          :deactivatedMaxPctZone="queryPaneMaxPctHeight - (100 - queryPaneMaxPctHeight) * 2"
        >
          <template #pane-left>
            <ResizablePanels
              v-model="editorPanePctWidth"
              class="editor__content"
              :minPercent="editorPaneMinPctWidth"
              :maxPercent="100 - $helpers.pxToPct({ px: 100, containerPx: panesDim.width })"
              :boundary="panesDim.width"
              split="vert"
              :disable="isChartMaximized || !showVisChart"
            >
              <template #pane-left>
                <SqlEditor
                  ref="editorRef"
                  v-model="sql"
                  :isTabMoveFocus="tab_moves_focus"
                  class="editor pt-2"
                  :completionItems="completionItems"
                  isKeptAlive
                  :customActions="EDITOR_ACTIONS"
                  supportCustomDelimiter
                  @toggle-tab-focus-mode="dispatchEvt(CTRL_M)"
                  @on-selection="onSelectText"
                />
              </template>
              <template #pane-right>
                <ChartPane
                  v-if="showVisChart"
                  v-model="chartOpt"
                  :containerHeight="chartContainerHeight"
                  :chartTypes="CHART_TYPE_MAP"
                  :axisTypes="CHART_AXIS_TYPE_MAP"
                  class="chart-pane border-left--table-border"
                  @close-chart="setDefChartOptState"
                />
              </template>
            </ResizablePanels>
          </template>
          <template #pane-right>
            <QueryResultCtr
              :dim="resultPaneDim"
              class="query-result-ctr"
              :queryTab="queryTab"
              :queryTabConn="queryTabConn"
              :queryTabTmp="queryTabTmp"
              :dataTableProps="{
                placeToEditor,
                onDragging: draggingTxt,
                onDragend: dropTxtToEditor,
              }"
            />
          </template>
        </ResizablePanels>
      </template>
      <template #pane-right>
        <ChartConfig
          v-if="isVisSidebarShown"
          v-model="chartOpt"
          :chartTypes="CHART_TYPE_MAP"
          :axisTypes="CHART_AXIS_TYPE_MAP"
          :queryModes="QUERY_MODE_MAP"
          :resultSets="resultSets"
          class="chart-config border-left--table-border"
        />
      </template>
    </ResizablePanels>
  </div>
</template>

<style lang="scss" scoped>
.editor,
.chart-config,
.query-result-ctr,
.chart-pane {
  width: 100%;
  height: 100%;
}
</style>
