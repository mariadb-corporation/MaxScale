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
import SchemaFormDlg from '@wkeComps/QueryEditor/SchemaFormDlg.vue'
import CreateNode from '@wkeComps/QueryEditor/CreateNode.vue'
import schemaSidebarService from '@wsServices/schemaSidebarService'
import queryTabService from '@wsServices/queryTabService'
import tblEditorService from '@wsServices/tblEditorService'
import queryResultService from '@wsServices/queryResultService'
import queryConnService from '@wsServices/queryConnService'
import schemaNodeHelper from '@/utils/schemaNodeHelper'
import resultSetExtractor from '@/utils/resultSetExtractor'
import { getChildNodes, queryDDL, exeSql } from '@/store/queryHelper'
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

const { TBL_EDITOR, INSIGHT_VIEWER, SQL_EDITOR, DDL_EDITOR } = QUERY_TAB_TYPE_MAP
const { SCHEMA, TBL, VIEW, SP, FN, TRIGGER } = NODE_TYPE_MAP

const store = useStore()
const typy = useTypy()
const { quotingIdentifier } = useHelpers()

const toolbarRef = ref(null)
const toolbarHeight = ref(60)
const actionName = ref('')
const isSchemaFormDlgOpened = ref(false)
const schemaFormData = ref({})
const isAlteringSchema = ref(false)

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
const activeRequestConfig = computed(() => Worksheet.getters('activeRequestConfig'))
const activeDb = computed(() => typy(props.activeQueryTabConn, 'active_db').safeString)

async function fetchSchemas() {
  await schemaSidebarService.fetchSchemas()
}

async function loadChildren(nodeGroup) {
  const { id: connId } = props.activeQueryTabConn
  const children = await getChildNodes({ connId, nodeGroup, config: activeRequestConfig.value })
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
  const [error] = await exeSql({
    connId: activeQueryTabConnId.value,
    sql: exec_sql_dlg.value.sql,
    action: actionName.value,
  })
  store.commit('workspace/SET_EXEC_SQL_DLG', { ...exec_sql_dlg.value, error })
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

/**
 * @param {object} param
 * @param {NODE_CTX_TYPE_MAP} param.type
 * @param {object} [param.parentNameData]
 */
async function handleCreateNode({ type, parentNameData }) {
  switch (type) {
    case VIEW:
    case TRIGGER:
    case SP:
    case FN:
    case TBL: {
      const schema = parentNameData[SCHEMA]
      await queryTabService.handleAdd({
        query_editor_id: props.queryEditorId,
        name: `Create ${type}`,
        type: type === TBL ? TBL_EDITOR : DDL_EDITOR,
        schema: quotingIdentifier(schema),
      })
      if (type === TBL) await tblEditorService.genNewTable(schema)
      else
        DdlEditor.update({
          where: QueryEditor.getters('activeQueryTabId'),
          data: { sql: handleGetDdlTemplate({ type, parentNameData }), type },
        })
      break
    }
    case SCHEMA:
      isSchemaFormDlgOpened.value = true
      schemaFormData.value = { name: 'new_schema', charset: '', collation: '', comment: '' }
      isAlteringSchema.value = false
      break
  }
}

async function handleAlterNode({ node, spec }) {
  const { type } = node
  const targetNode = schemaNodeHelper.minimizeNode(node)
  switch (type) {
    case TBL:
    case VIEW:
    case TRIGGER:
    case SP:
    case FN: {
      await queryTabService.handleAdd({
        query_editor_id: props.queryEditorId,
        name: `Alter ${targetNode.name}`,
        type: type === TBL ? TBL_EDITOR : DDL_EDITOR,
        schema: getSchemaIdentifier(targetNode),
      })
      if (type === TBL) await tblEditorService.loadTblStructureData({ node: targetNode, spec })
      else {
        const [e, resultSets] = await queryDDL({
          connId: activeQueryTabConnId.value,
          type,
          qualifiedNames: [targetNode.qualified_name],
          config: activeRequestConfig.value,
        })
        if (!e)
          DdlEditor.update({
            where: QueryEditor.getters('activeQueryTabId'),
            data: {
              active_node: targetNode,
              sql: resultSetExtractor.getDdl({
                type,
                resultSet: typy(resultSets, '[0]').safeObjectOrEmpty,
              }),
              type,
            },
          })
      }
      break
    }
    case SCHEMA: {
      const { DEFAULT_CHARACTER_SET_NAME, DEFAULT_COLLATION_NAME, SCHEMA_COMMENT } = node.data || {}
      isSchemaFormDlgOpened.value = true
      schemaFormData.value = {
        name: targetNode.name,
        charset: DEFAULT_CHARACTER_SET_NAME,
        collation: DEFAULT_COLLATION_NAME,
        comment: SCHEMA_COMMENT,
      }
      isAlteringSchema.value = true
      break
    }
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
          <CreateNode
            :activeDb="activeDb"
            :disabled="isSidebarDisabled || !hasConn"
            @create-node="handleCreateNode"
          />
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
            color="primary"
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
      :activeDb="activeDb"
      :schemaSidebar="schemaSidebar"
      :search="filterTxt"
      :loadChildren="loadChildren"
      @get-node-data="handleFetchNodePrvwData"
      @use-db="useDb"
      @drop-action="handleOpenExecSqlDlg"
      @truncate-tbl="handleOpenExecSqlDlg"
      @gen-erd="handleShowGenErdDlg([$event.qualified_name])"
      @view-node-insights="viewNodeInsights"
      @create-node="handleCreateNode"
      @alter-node="handleAlterNode"
      v-bind="$attrs"
    />
    <SchemaFormDlg
      v-model="isSchemaFormDlgOpened"
      :data="schemaFormData"
      :isAltering="isAlteringSchema"
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
