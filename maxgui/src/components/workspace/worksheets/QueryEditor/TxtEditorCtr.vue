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
import TxtEditor from '@wsModels/TxtEditor'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import TxtEditorToolbarCtr from '@wkeComps/QueryEditor/TxtEditorToolbarCtr.vue'
import ChartConfig from '@wkeComps/QueryEditor/ChartConfig.vue'
import ChartPane from '@wkeComps/QueryEditor/ChartPane.vue'
import QueryResultCtr from '@wkeComps/QueryEditor/QueryResultCtr.vue'
import queryConnService from '@wsServices/queryConnService'
import schemaNodeHelper from '@/utils/schemaNodeHelper'
import { QUERY_MODES, SQL_CHART_TYPES, CHART_AXIS_TYPES } from '@/constants/workspace'
import { WS_EDITOR_KEY } from '@/constants/injectionKeys'

const props = defineProps({
  dim: { type: Object, required: true },
  queryEditorTmp: { type: Object, required: true },
  queryTab: { type: Object, required: true },
})

const store = useStore()
const { t } = useI18n()
const typy = useTypy()
const { pxToPct, getAppEle } = useHelpers()
const dispatchEvt = useEventDispatcher(WS_EDITOR_KEY)

const TOOLBAR_HEIGHT = 28
const VIS_SIDEBAR_WIDTH = 250
let mouseDropDOM = null,
  mouseDropWidget = null

const editorPanePctWidth = ref(100)
const editorRef = ref(null)
const chartOpt = ref({})
const selectedQueryTxt = ref('')

const query_pane_pct_height = computed(() => store.state.prefAndStorage.query_pane_pct_height)
const tab_moves_focus = computed(() => store.state.prefAndStorage.tab_moves_focus)
const identifier_auto_completion = computed(
  () => store.state.prefAndStorage.identifier_auto_completion
)
const snippetCompletionItems = computed(
  () => store.getters['prefAndStorage/snippetCompletionItems']
)
const queryTabConn = computed(() => queryConnService.findQueryTabConn(props.queryTab.id))
const queryTabTmp = computed(() => QueryTabTmp.find(props.queryTab.id) || {})
const prvwDataResultSets = computed(() => {
  const resSets = []
  const { prvw_data, prvw_data_details, previewing_node } = queryTabTmp.value
  const nodeQualifiedName = typy(previewing_node, 'qualified_name').safeString
  const addToResSets = (data, mode) => {
    if (!typy(data).isEmptyObject)
      resSets.push({ id: `${t(mode)} of ${nodeQualifiedName}`, ...data })
  }
  addToResSets(prvw_data, 'previewData')
  addToResSets(prvw_data_details, 'viewDetails')
  return resSets
})
const userResultSets = computed(() => {
  const results = typy(queryTabTmp.value, 'query_results.data.attributes.results').safeArray
  let count = 0
  return results.reduce((resultSets, res) => {
    if (res.data) {
      ++count
      resultSets.push({ id: `RESULT SET ${count}`, ...res })
    }
    return resultSets
  }, [])
})
const resultSets = computed(() => [...userResultSets.value, ...prvwDataResultSets.value])
const activeSchema = computed(() => typy(queryTabConn.value, 'active_db').safeString)
const schemaTree = computed(() => {
  const tree = typy(props.queryEditorTmp, 'db_tree').safeArray
  if (identifier_auto_completion.value && activeSchema.value)
    return tree.filter((n) => n.qualified_name !== activeSchema.value)
  return tree
})
const schemaTreeCompletionItems = computed(() =>
  schemaNodeHelper.genNodeCompletionItems(schemaTree.value)
)
const activeSchemaIdentifierCompletionItems = computed(
  () => typy(queryTabTmp.value, 'schema_identifier_names_completion_items').safeArray
)
const completionItems = computed(() => [
  ...schemaTreeCompletionItems.value,
  ...activeSchemaIdentifierCompletionItems.value,
  ...snippetCompletionItems.value,
])
const showVisChart = computed(() =>
  Boolean(
    typy(chartOpt.value, 'type').safeString &&
      typy(chartOpt.value, 'chartData.labels').safeArray.length
  )
)
const isChartMaximized = computed(() => typy(chartOpt.value, 'isMaximized').safeBoolean)
const panesDim = computed(() => ({
  width: props.dim.width,
  height: props.dim.height - TOOLBAR_HEIGHT,
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
const txtEditor = computed(() => TxtEditor.find(props.queryTab.id) || {})
const queryTxt = computed({
  get: () => typy(txtEditor.value, 'query_txt').safeString,
  set: (v) => TxtEditor.update({ where: props.queryTab.id, data: { query_txt: v } }),
})
const resultPaneDim = computed(() => ({
  width: panesDim.value.width - (isVisSidebarShown.value ? VIS_SIDEBAR_WIDTH : 0),
  height: (panesDim.value.height * (100 - queryPanePctHeight.value)) / 100,
}))
const isVisSidebarShown = computed(() => typy(txtEditor.value, 'is_vis_sidebar_shown').safeBoolean)
const isTabMoveFocus = computed({
  get: () => tab_moves_focus.value,
  set: (v) => store.commit('prefAndStorage/SET_TAB_MOVES_FOCUS', v),
})

watch(isChartMaximized, (v) => {
  editorPanePctWidth.value = v ? editorPaneMinPctWidth.value : 50
})
watch(showVisChart, (v) => {
  editorPanePctWidth.value = v ? 50 : 100
})

onMounted(() => setDefChartOptState())

function toggleTabMoveFocus() {
  const editor = typy(editorRef.value, 'getEditorInstance').safeFunction()
  if (!typy(editor).isEmptyObject) editor.trigger('', 'editor.action.toggleTabFocusMode')
}

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

// editor related functions
function placeToEditor(text) {
  editorRef.value.insertAtCursor({ text })
}

function handleGenMouseDropWidget(dropTarget) {
  /**
   *  Setting text cusor to all elements as a fallback method for firefox
   *  as monaco editor will fail to get dropTarget position in firefox
   *  So only add mouseDropWidget when user agent is not firefox
   */
  if (navigator.userAgent.includes('Firefox')) {
    getAppEle().classList.add(dropTarget ? 'cursor--text--all' : 'cursor--grab--all')
  } else {
    const { getEditorInstance, monaco } = editorRef.value
    const editor = getEditorInstance()
    getAppEle().classList.remove('cursor--grab--all')
    if (dropTarget) {
      const preference = monaco.editor.ContentWidgetPositionPreference.EXACT
      if (!mouseDropDOM) {
        mouseDropDOM = document.createElement('div')
        mouseDropDOM.style.pointerEvents = 'none'
        mouseDropDOM.style.borderLeft = '2px solid #424f62'
        mouseDropDOM.innerHTML = '&nbsp;'
      }
      mouseDropWidget = {
        mouseDropDOM: null,
        getId: () => 'drag',
        getDomNode: () => mouseDropDOM,
        getPosition: () => ({
          position: dropTarget.position,
          preference: [preference, preference],
        }),
      }
      //remove the prev cusor widget first then add
      editor.removeContentWidget(mouseDropWidget)
      editor.addContentWidget(mouseDropWidget)
    } else if (mouseDropWidget) editor.removeContentWidget(mouseDropWidget)
  }
}

function draggingTxt(e) {
  const { getEditorInstance } = editorRef.value
  // build mouseDropWidget
  const dropTarget = getEditorInstance().getTargetAtClientPoint(e.clientX, e.clientY)
  handleGenMouseDropWidget(dropTarget)
}

function dropTxtToEditor(e) {
  if (e.target.textContent) {
    const { getEditorInstance, monaco, insertAtCursor } = editorRef.value
    const editor = getEditorInstance()
    const dropTarget = editor.getTargetAtClientPoint(e.clientX, e.clientY)

    if (dropTarget) {
      const dropPos = dropTarget.position
      // create range
      const range = new monaco.Range(
        dropPos.lineNumber,
        dropPos.column,
        dropPos.lineNumber,
        dropPos.column
      )
      const text = e.target.textContent.trim()
      insertAtCursor({ text, range })
      if (mouseDropWidget) editor.removeContentWidget(mouseDropWidget)
    }
    getAppEle().className = ''
  }
}

function onSelectText(v) {
  selectedQueryTxt.value = v
}

defineExpose({ placeToEditor, draggingTxt, dropTxtToEditor })
</script>

<template>
  <div class="d-flex flex-column fill-height">
    <TxtEditorToolbarCtr
      class="d-flex"
      :height="TOOLBAR_HEIGHT"
      :queryTab="queryTab"
      :queryTabTmp="queryTabTmp"
      :queryTabConn="queryTabConn"
      :queryTxt="queryTxt"
      :selectedQueryTxt="selectedQueryTxt"
      :isVisSidebarShown="isVisSidebarShown"
      @disable-tab-move-focus="toggleTabMoveFocus"
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
                  v-model="queryTxt"
                  v-model:isTabMoveFocus="isTabMoveFocus"
                  class="editor pt-2"
                  :completionItems="completionItems"
                  isKeptAlive
                  @on-selection="onSelectText"
                  @shortkey="dispatchEvt"
                />
              </template>
              <template #pane-right>
                <ChartPane
                  v-if="showVisChart"
                  v-model="chartOpt"
                  :containerHeight="chartContainerHeight"
                  :chartTypes="SQL_CHART_TYPES"
                  :axisTypes="CHART_AXIS_TYPES"
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
          :chartTypes="SQL_CHART_TYPES"
          :axisTypes="CHART_AXIS_TYPES"
          :queryModes="QUERY_MODES"
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
