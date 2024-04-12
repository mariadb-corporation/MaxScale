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
import InsightViewer from '@wsModels/InsightViewer'
import AlterEditor from '@wsModels/AlterEditor'
import QueryConn from '@wsModels/QueryConn'
import QueryResult from '@wsModels/QueryResult'
import QueryTab from '@wsModels/QueryTab'
import SchemaSidebar from '@wsModels/SchemaSidebar'
import Worksheet from '@wsModels/Worksheet'
import SchemaTreeCtr from '@wkeComps/QueryEditor/SchemaTreeCtr.vue'
import workspaceService from '@/services/workspaceService'
import schemaNodeHelper from '@/utils/schemaNodeHelper'
import { getChildNodes } from '@/store/queryHelper'
import { NODE_TYPES, QUERY_TAB_TYPES } from '@/constants/workspace'

defineOptions({ inheritAttrs: false })

const props = defineProps({
  queryEditorId: { type: String, required: true },
  queryEditorTmp: { type: Object, required: true },
  activeQueryTabId: { type: String, required: true },
  activeQueryTabConn: { type: Object, required: true },
  height: { type: Number, required: true },
})

const store = useStore()
const typy = useTypy()
const { quotingIdentifier } = useHelpers()

const toolbarRef = ref(null)
const toolbarHeight = ref(60)

const schemaTreeHeight = computed(() => props.height - toolbarHeight.value)
const is_sidebar_collapsed = computed(() => store.state.prefAndStorage.is_sidebar_collapsed)
const exec_sql_dlg = computed(() => store.state.mxsWorkspace.exec_sql_dlg)

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

let actionName = ref('')

async function fetchSchemas() {
  await SchemaSidebar.dispatch('fetchSchemas')
}

async function loadChildren(nodeGroup) {
  const config = Worksheet.getters('activeRequestConfig')
  const { id: connId } = props.activeQueryTabConn
  const children = await getChildNodes({ connId, nodeGroup, config })
  return children
}

async function useDb(schema) {
  await QueryConn.dispatch('useDb', {
    connName: typy(props.activeQueryTabConn, 'meta.name').safeString,
    connId: activeQueryTabConnId.value,
    schema,
  })
}

async function fetchNodePrvwData({ query_mode, qualified_name }) {
  QueryResult.dispatch('clearDataPreview')
  QueryResult.update({ where: props.activeQueryTabId, data: { query_mode } })
  await QueryResult.dispatch('fetchPrvw', { qualified_name: qualified_name, query_mode })
}

function getSchemaIdentifier(node) {
  return quotingIdentifier(schemaNodeHelper.getSchemaName(node))
}

async function onAlterTable(node) {
  const config = Worksheet.getters('activeRequestConfig')
  await QueryTab.dispatch('handleAddQueryTab', {
    query_editor_id: props.queryEditorId,
    name: `ALTER ${node.name}`,
    type: QUERY_TAB_TYPES.ALTER_EDITOR,
    schema: getSchemaIdentifier(node),
  })
  await store.dispatch('ddlEditor/queryDdlEditorSuppData', {
    connId: activeQueryTabConnId.value,
    config,
  })
  await AlterEditor.dispatch('queryTblCreationInfo', node)
}

function handleOpenExecSqlDlg(sql) {
  store.commit('mxsWorkspace/SET_EXEC_SQL_DLG', {
    ...exec_sql_dlg.value,
    is_opened: true,
    editor_height: 200,
    sql,
    on_exec: confirmExeStatements,
    after_cancel: clearExeStatementsResult,
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

function clearExeStatementsResult() {
  store.commit('mxsWorkspace/SET_EXEC_SQL_DLG', { ...exec_sql_dlg.value, result: null })
}

function handleShowGenErdDlg(preselectedSchemas) {
  store.commit('mxsWorkspace/SET_GEN_ERD_DLG', {
    is_opened: true,
    preselected_schemas: typy(preselectedSchemas).safeArray,
    connection: QueryConn.query().where('query_editor_id', props.queryEditorId).first() || {},
    gen_in_new_ws: true,
  })
}

async function viewNodeInsights(node) {
  let name = `Analyze ${node.name}`
  const { VIEW, TRIGGER, SP, FN } = NODE_TYPES
  switch (node.type) {
    case VIEW:
    case TRIGGER:
    case SP:
    case FN:
      name = `Show create ${node.name} `
      break
  }
  await QueryTab.dispatch('handleAddQueryTab', {
    query_editor_id: props.queryEditorId,
    name,
    type: QUERY_TAB_TYPES.INSIGHT_VIEWER,
    schema: getSchemaIdentifier(node),
  })
  InsightViewer.update({ where: props.activeQueryTabId, data: { active_node: node } })
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
      :class="[isCollapsed ? 'px-1 pb-1' : 'px-3 pb-3']"
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
            class="reload-schemas"
            icon
            density="compact"
            variant="text"
            :disabled="disableReload"
            :color="disableReload ? '' : 'primary'"
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
      @get-node-data="fetchNodePrvwData"
      @use-db="useDb"
      @drop-action="handleOpenExecSqlDlg"
      @alter-tbl="onAlterTable"
      @truncate-tbl="handleOpenExecSqlDlg"
      @gen-erd="handleShowGenErdDlg([$event.qualified_name])"
      @view-node-insights="viewNodeInsights"
      v-bind="$attrs"
    />
  </div>
</template>

<style lang="scss" scoped>
.sidebar-wrapper {
  width: 100%;
  .sidebar-toolbar {
    height: 60px;
    padding-top: 2px;
    &__title {
      font-size: 12px;
      margin-right: auto;
    }
    :deep(.filter-objects) {
      input {
        font-size: 12px;
      }
    }
  }
}
</style>
