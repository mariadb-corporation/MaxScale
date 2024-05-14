<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import QueryEditor from '@wsModels/QueryEditor'
import QueryEditorTmp from '@wsModels/QueryEditorTmp'
import QueryTab from '@wsModels/QueryTab'
import SidebarCtr from '@wkeComps/QueryEditor/SidebarCtr.vue'
import QueryTabNavCtr from '@wkeComps/QueryEditor/QueryTabNavCtr.vue'
import TxtEditorCtr from '@wkeComps/QueryEditor/TxtEditorCtr.vue'
import AlterTableEditor from '@wkeComps/QueryEditor/AlterTableEditor.vue'
import InsightViewer from '@wkeComps/QueryEditor/InsightViewer.vue'
import queryEditorService from '@wsServices/queryEditorService'
import queryConnService from '@wsServices/queryConnService'
import { QUERY_TAB_TYPES } from '@/constants/workspace'

const props = defineProps({
  ctrDim: { type: Object, required: true },
  queryEditorId: { type: String, required: true },
})

const store = useStore()
const typy = useTypy()
const { pxToPct } = useHelpers()
const { SQL_EDITOR, ALTER_EDITOR } = QUERY_TAB_TYPES

const QUERY_TAB_CTR_HEIGHT = 30

const isInitializing = ref(true)
const editorRefs = ref([])

const is_sidebar_collapsed = computed(() => store.state.prefAndStorage.is_sidebar_collapsed)
const sidebar_pct_width = computed(() => store.state.prefAndStorage.sidebar_pct_width)

const queryEditor = computed(() => QueryEditor.query().find(props.queryEditorId) || {})
const queryEditorTmp = computed(() => QueryEditorTmp.find(props.queryEditorId) || {})
const queryTabs = computed(
  () =>
    QueryTab.query()
      .where((t) => t.query_editor_id === props.queryEditorId)
      .get() || []
)
const activeQueryTabId = computed(() => typy(queryEditor.value, 'active_query_tab_id').safeString)
const activeQueryTabConn = computed(() => queryConnService.findQueryTabConn(activeQueryTabId.value))
const activeQueryTabConnId = computed(() => typy(activeQueryTabConn.value, 'id').safeString)
const minSidebarPct = computed(() => pxToPct({ px: 40, containerPx: props.ctrDim.width }))
const deactivatedMinSizeBarPoint = computed(() => minSidebarPct.value * 3)
const maxSidebarPct = computed(() => 100 - pxToPct({ px: 370, containerPx: props.ctrDim.width }))
const defSidebarPct = computed(() => pxToPct({ px: 240, containerPx: props.ctrDim.width }))
const sidebarPct = computed({
  get: () => {
    if (is_sidebar_collapsed.value) return minSidebarPct.value
    return sidebar_pct_width.value
  },
  set: (v) => store.commit('prefAndStorage/SET_SIDEBAR_PCT_WIDTH', v),
})
const sidebarWidth = computed(() => (props.ctrDim.width * sidebarPct.value) / 100)
const editorDim = computed(() => ({
  width: props.ctrDim.width - sidebarWidth.value,
  height: props.ctrDim.height - QUERY_TAB_CTR_HEIGHT,
}))

/**
 * When sidebar is expanded by clicking the expand button,
 * if the current sidebar percent width <= the minimum sidebar percent
 * assign the default percent
 */
watch(is_sidebar_collapsed, (v) => {
  if (!v && sidebarPct.value <= minSidebarPct.value) sidebarPct.value = defSidebarPct.value
})

watch(
  activeQueryTabConnId,
  async (v, oV) => {
    if (v !== oV) {
      await queryEditorService.initialFetch()
      isInitializing.value = false
    } else if (!v) isInitializing.value = false
  },
  { deep: true, immediate: true }
)
onMounted(() => nextTick(() => handleSetDefSidebarPct()))

// panes dimension/percentages calculation functions
function handleSetDefSidebarPct() {
  if (!sidebarPct.value) sidebarPct.value = defSidebarPct.value
}

function onResizing(v) {
  //auto collapse sidebar
  if (v <= minSidebarPct.value) store.commit('prefAndStorage/SET_IS_SIDEBAR_COLLAPSED', true)
  else if (v >= deactivatedMinSizeBarPoint.value)
    store.commit('prefAndStorage/SET_IS_SIDEBAR_COLLAPSED', false)
}

function getComponentType(queryTab) {
  let data = {
    component: '',
    props: {
      queryEditorTmp: queryEditorTmp.value,
      queryTab,
      dim: editorDim.value,
    },
  }
  switch (queryTab.type) {
    case SQL_EDITOR:
      data.component = TxtEditorCtr
      break
    case ALTER_EDITOR:
      data.component = AlterTableEditor
      break
    default:
      data.component = InsightViewer
      data.props = { queryTab, dim: editorDim.value }
  }
  return data
}
</script>

<template>
  <VProgressLinear v-if="isInitializing" indeterminate color="primary" />
  <ResizablePanels
    v-else
    v-model="sidebarPct"
    class="query-view__content"
    :boundary="ctrDim.width"
    :minPercent="minSidebarPct"
    :deactivatedMinPctZone="deactivatedMinSizeBarPoint"
    :maxPercent="maxSidebarPct"
    split="vert"
    progress
    @resizing="onResizing"
  >
    <template #pane-left>
      <!-- TODO: use provide/inject to emit event to editor -->
      <SidebarCtr
        :queryEditorId="queryEditorId"
        :queryEditorTmp="queryEditorTmp"
        :activeQueryTabId="activeQueryTabId"
        :activeQueryTabConn="activeQueryTabConn"
        :height="ctrDim.height"
        @place-to-editor="typy(editorRefs, '[0].placeToEditor').safeFunction($event)"
        @on-dragging="typy(editorRefs, '[0].draggingTxt').safeFunction($event)"
        @on-dragend="typy(editorRefs, '[0].dropTxtToEditor').safeFunction($event)"
      />
    </template>
    <template #pane-right>
      <div class="d-flex flex-column fill-height">
        <QueryTabNavCtr
          :queryEditorId="queryEditorId"
          :activeQueryTabId="activeQueryTabId"
          :activeQueryTabConn="activeQueryTabConn"
          :queryTabs="queryTabs"
          :height="QUERY_TAB_CTR_HEIGHT"
        >
          <template v-for="(_, name) in $slots" #[name]="slotData">
            <slot :name="name" v-bind="slotData" />
          </template>
        </QueryTabNavCtr>
        <KeepAlive v-for="queryTab in queryTabs" :key="`${queryTab.id}-${queryTab.type}`" max="10">
          <template v-if="activeQueryTabId === queryTab.id">
            <component
              ref="editorRefs"
              :is="getComponentType(queryTab).component"
              v-bind="getComponentType(queryTab).props"
            />
          </template>
        </KeepAlive>
      </div>
    </template>
  </ResizablePanels>
</template>
