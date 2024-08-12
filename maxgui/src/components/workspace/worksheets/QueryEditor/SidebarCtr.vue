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
import InsightViewer from '@wsModels/InsightViewer'
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import QueryResult from '@wsModels/QueryResult'
import QueryTab from '@wsModels/QueryTab'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import SchemaSidebar from '@wsModels/SchemaSidebar'
import Worksheet from '@wsModels/Worksheet'
import SchemaTreeCtr from '@wkeComps/QueryEditor/SchemaTreeCtr.vue'
import workspaceService from '@wsServices/workspaceService'
import schemaSidebarService from '@wsServices/schemaSidebarService'
import queryTabService from '@wsServices/queryTabService'
import alterEditorService from '@wsServices/alterEditorService'
import queryResultService from '@wsServices/queryResultService'
import queryConnService from '@wsServices/queryConnService'
import schemaNodeHelper from '@/utils/schemaNodeHelper'
import { getChildNodes } from '@/store/queryHelper'
import ddlTemplate from '@/utils/ddlTemplate'
import { NODE_TYPE_MAP, QUERY_TAB_TYPE_MAP } from '@/constants/workspace'

defineOptions({ inheritAttrs: false })

const props = defineProps({
  queryEditorId: { type: String, required: true },
  queryEditorTmp: { type: Object, required: true },
  activeQueryTabId: { type: String, required: true },
  activeQueryTabConn: { type: Object, required: true },
  height: { type: Number, required: true },
})

const { ALTER_EDITOR, INSIGHT_VIEWER, SQL_EDITOR, DDL_EDITOR } = QUERY_TAB_TYPE_MAP
const { SCHEMA, TBL, VIEW, SP, FN, TRIGGER } = NODE_TYPE_MAP

const store = useStore()
const typy = useTypy()
const { quotingIdentifier } = useHelpers()

const toolbarRef = ref(null)
const toolbarHeight = ref(60)
const actionName = ref('')

const schemaTreeHeight = computed(() => props.height - toolbarHeight.value)
const is_sidebar_collapsed = computed(() => store.state.prefAndStorage.is_sidebar_collapsed)
const exec_sql_dlg = computed(() => store.state.workspace.exec_sql_dlg)

const isCollapsed = computed({
  get: () => is_sidebar_collapsed.value,
  set: (v) => store.commit('prefAndStorage/SET_IS_SIDEBAR_COLLAPSED', v),
})
const schemaSidebar = computed(() => SchemaSidebar.find(props.queryEditorId) || {})
const filterTxt = computed({
  get: () => schemaSidebar.value.filter_txt || '',
  set: (v) => SchemaSidebar.update({ where: props.queryEditorId, data: { filter_txt: v } }),
})
const activeQueryTabConnId = computed(() => typy(props.activeQueryTabConn, 'id').safeString)
const isLoadingDbTree = computed(() => typy(props.queryEditorTmp.loading_db_tree).safeBoolean)
const hasConn = computed(() => Boolean(activeQueryTabConnId.value))
const disableReload = computed(() => !hasConn.value || isLoadingDbTree.value)
const isSidebarDisabled = computed(() => props.activeQueryTabConn.is_busy || isLoadingDbTree.value)
const activeQueryTab = computed(() => QueryTab.find(props.activeQueryTabId) || {})
const isSqlEditor = computed(() => typy(activeQueryTab.value, 'type').safeString === SQL_EDITOR)

async function fetchSchemas() {
  await schemaSidebarService.fetchSchemas()
}

async function loadChildren(nodeGroup) {
  const config = Worksheet.getters('activeRequestConfig')
  const { id: connId } = props.activeQueryTabConn
  const children = await getChildNodes({ connId, nodeGroup, config })
  return children
}

async function useDb(schema) {
  await queryConnService.useDb({
    connName: typy(props.activeQueryTabConn, 'meta.name').safeString,
    connId: activeQueryTabConnId.value,
    schema,
  })
}

async function handleFetchNodePrvwData(param) {
  if (!isSqlEditor.value)
    await queryTabService.handleAdd({
      query_editor_id: props.queryEditorId,
      type: SQL_EDITOR,
      schema: getSchemaIdentifier(param.node),
    })
  await fetchNodePrvwData(param)
}

async function fetchNodePrvwData({ mode, node }) {
  clearDataPreview()
  QueryResult.update({ where: props.activeQueryTabId, data: { query_mode: mode } })
  QueryTabTmp.update({ where: props.activeQueryTabId, data: { active_node: node } })
  await queryResultService.queryPrvw({ qualified_name: node.qualified_name, query_mode: mode })
}

/**
 * This action clears prvw_data and prvw_data_details to empty object.
 * Call this action when user selects option in the sidebar.
 * This ensure sub-tabs in Data Preview tab are generated with fresh data
 */
function clearDataPreview() {
  QueryTabTmp.update({
    where: QueryEditor.getters('activeQueryTabId'),
    data(obj) {
      obj.prvw_data = {}
      obj.prvw_data_details = {}
    },
  })
}

function getSchemaIdentifier(node) {
  return quotingIdentifier(schemaNodeHelper.getSchemaName(node))
}

async function onAlterTable({ node, spec }) {
  await queryTabService.handleAdd({
    query_editor_id: props.queryEditorId,
    name: `ALTER ${node.name}`,
    type: ALTER_EDITOR,
    schema: getSchemaIdentifier(node),
  })
  await alterEditorService.queryTblCreationInfo({ node, spec })
}

function handleOpenExecSqlDlg(sql) {
  store.commit('workspace/SET_EXEC_SQL_DLG', {
    ...exec_sql_dlg.value,
    is_opened: true,
    editor_height: 200,
    sql,
    on_exec: confirmExeStatements,
  })
  actionName.value = sql.slice(0, -1)
}

async function confirmExeStatements() {
  await workspaceService.exeStatement({
    connId: activeQueryTabConnId.value,
    sql: exec_sql_dlg.value.sql,
    action: actionName.value,
  })
}

function handleShowGenErdDlg(preselectedSchemas) {
  store.commit('workspace/SET_GEN_ERD_DLG', {
    is_opened: true,
    preselected_schemas: typy(preselectedSchemas).safeArray,
    connection: QueryConn.query().where('query_editor_id', props.queryEditorId).first() || {},
    gen_in_new_ws: true,
  })
}

async function viewNodeInsights(node) {
  let name = `Analyze ${node.name}`
  switch (node.type) {
    case VIEW:
    case TRIGGER:
    case SP:
    case FN:
      name = `Show create ${node.name} `
      break
  }
  await queryTabService.handleAdd({
    query_editor_id: props.queryEditorId,
    name,
    type: INSIGHT_VIEWER,
    schema: getSchemaIdentifier(node),
  })
  InsightViewer.update({ where: props.activeQueryTabId, data: { active_node: node } })
}

function handleGetDdlTemplate({ type, parentNameData }) {
  const schema = parentNameData[SCHEMA]
  switch (type) {
    case VIEW:
      return ddlTemplate.createView(schema)
    case SP:
      return ddlTemplate.createSP(schema)
    case FN:
      return ddlTemplate.createFN(schema)
    case TRIGGER:
      return ddlTemplate.createTrigger({ schema, tbl: parentNameData[TBL] })
    default:
      return ''
  }
}

async function handleCreateNode({ type, parentNameData }) {
  switch (type) {
    case VIEW:
    case TRIGGER:
    case SP:
    case FN: {
      await queryTabService.handleAdd({
        query_editor_id: props.queryEditorId,
        name: `Create ${type}`,
        type: DDL_EDITOR,
        schema: quotingIdentifier(parentNameData[SCHEMA]),
      })
      DdlEditor.update({
        where: QueryEditor.getters('activeQueryTabId'),
        data: { sql: handleGetDdlTemplate({ type, parentNameData }), type },
      })
      break
    }
    //TODO: Add function to create TBL and SCHEMA
    case TBL:
    case SCHEMA:
      break
  }
}

function setToolbarHeight() {
  const { height } = toolbarRef.value.getBoundingClientRect()
  toolbarHeight.value = height
}

onMounted(() => nextTick(() => setToolbarHeight()))
</script>

<template>
  <div
    class="sidebar-wrapper d-flex flex-column fill-height border-right--table-border"
    :class="{ 'cursor--wait': isSidebarDisabled }"
  >
    <div
      ref="toolbarRef"
      class="sidebar-toolbar"
      :class="[isCollapsed ? 'px-1 pb-1' : 'px-3 pb-1']"
    >
      <div class="d-flex align-center justify-center">
        <span
          v-if="!isCollapsed"
          class="text-small-text sidebar-toolbar__title d-inline-block text-truncate text-capitalize"
        >
          {{ $t('schemas', 2) }}
        </span>
        <template v-if="!isCollapsed">
          <TooltipBtn
            class="visualize-schemas"
            icon
            density="compact"
            variant="text"
            :disabled="isSidebarDisabled || !hasConn"
            :color="isSidebarDisabled ? '' : 'primary'"
            @click="handleShowGenErdDlg()"
          >
            <template #btn-content>
              <VIcon size="12" icon="mxs:reports" />
            </template>
            {{ $t('genErd') }}
          </TooltipBtn>
          <TooltipBtn
            icon
            density="compact"
            variant="text"
            :disabled="disableReload"
            :color="disableReload ? '' : 'primary'"
            data-test="reload-schemas"
            @click="fetchSchemas"
          >
            <template #btn-content>
              <VIcon size="12" icon="mxs:reload" />
            </template>
            {{ $t('reload') }}
          </TooltipBtn>
        </template>

        <TooltipBtn
          class="toggle-sidebar"
          icon
          density="compact"
          variant="text"
          color="primary"
          @click="isCollapsed = !isCollapsed"
        >
          <template #btn-content>
            <VIcon
              size="22"
              class="collapse-icon"
              :class="[isCollapsed ? 'rotate-right' : 'rotate-left']"
              icon="$mdiChevronDoubleDown"
            />
          </template>
          {{ isCollapsed ? $t('expand') : $t('collapse') }}
        </TooltipBtn>
      </div>
      <template v-if="!isCollapsed">
        <VTextField
          v-model="filterTxt"
          class="filter-objects"
          density="compact"
          :placeholder="$t('filterSchemaObjects')"
          :disabled="!hasConn"
          data-test="filter-objects"
          hide-details
        />
      </template>
    </div>
    <SchemaTreeCtr
      v-if="schemaTreeHeight"
      v-show="!isCollapsed"
      :height="schemaTreeHeight"
      :queryEditorId="queryEditorId"
      :activeQueryTabId="activeQueryTabId"
      :queryEditorTmp="queryEditorTmp"
      :activeQueryTabConn="activeQueryTabConn"
      :schemaSidebar="schemaSidebar"
      :search="filterTxt"
      :loadChildren="loadChildren"
      @get-node-data="handleFetchNodePrvwData"
      @use-db="useDb"
      @drop-action="handleOpenExecSqlDlg"
      @alter-tbl="onAlterTable"
      @truncate-tbl="handleOpenExecSqlDlg"
      @gen-erd="handleShowGenErdDlg([$event.qualified_name])"
      @view-node-insights="viewNodeInsights"
      @create-node="handleCreateNode"
      v-bind="$attrs"
    />
  </div>
</template>

<style lang="scss" scoped>
.sidebar-wrapper {
  width: 100%;
  .sidebar-toolbar {
    padding-top: 2px;
    &__title {
      font-size: 0.75rem;
      margin-right: auto;
    }
    :deep(.filter-objects) {
      input {
        font-size: 0.75rem !important;
      }
    }
  }
}
</style>
