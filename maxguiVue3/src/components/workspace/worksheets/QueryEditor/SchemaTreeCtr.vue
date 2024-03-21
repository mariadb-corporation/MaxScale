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
import QueryEditorTmp from '@wsModels/QueryEditorTmp'
import InsightViewer from '@wsModels/InsightViewer'
import AlterEditor from '@wsModels/AlterEditor'
import QueryTab from '@wsModels/QueryTab'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import SchemaSidebar from '@wsModels/SchemaSidebar'
import VirSchemaTree from '@wsComps/VirSchemaTree.vue'
import SchemaNodeIcon from '@wsComps/SchemaNodeIcon.vue'
import schemaNodeHelper from '@/utils/schemaNodeHelper'
import { NODE_TYPES, QUERY_MODES, NODE_CTX_TYPES, QUERY_TAB_TYPES } from '@/constants/workspace'

defineOptions({ inheritAttrs: false })
const props = defineProps({
  queryEditorId: { type: String, required: true },
  activeQueryTabId: { type: String, required: true },
  queryEditorTmp: { type: Object, required: true },
  activeQueryTabConn: { type: Object, required: true },
  filterTxt: { type: String, required: true },
  schemaSidebar: { type: Object, required: true },
  loadChildren: { type: Function, required: true },
})
const emit = defineEmits([
  'place-to-editor', //  v:string. Place text to editor
  'get-node-data', // { query_mode: string, qualified_name:string }
  'use-db', // v:string. qualified_name
  'drop-action', // v:string. sql
  'alter-tbl', //  v:object. Alterable table node
  'truncate-tbl', // v:string. sql
  'gen-erd', // v:object. Schema node
  'view-node-insights', // v:object. Either Schema or Table node.
  'on-dragging', // v:object. Event emitted from useDragAndDrop
  'on-dragend', // v:object. Event emitted from useDragAndDrop
])

const typy = useTypy()
const { t } = useI18n()
const {
  capitalizeFirstLetter,
  quotingIdentifier,
  copyTextToClipboard,
  lodash: { isEqual },
} = useHelpers()

const { ALTER_EDITOR, INSIGHT_VIEWER, SQL_EDITOR } = QUERY_TAB_TYPES
const { SCHEMA, TBL, VIEW, SP, FN, COL, IDX, TRIGGER } = NODE_TYPES
const { USE, VIEW_INSIGHTS, PRVW_DATA, PRVW_DATA_DETAILS, GEN_ERD, DROP, ALTER, TRUNCATE } =
  NODE_CTX_TYPES

const NODES_HAVE_CTX_MENU = Object.values(NODE_TYPES)
const TXT_OPS = [
  { title: t('placeToEditor'), children: genTxtOpts(NODE_CTX_TYPES.INSERT) },
  { title: t('copyToClipboard'), children: genTxtOpts(NODE_CTX_TYPES.CLIPBOARD) },
]

const { isDragging, dragTarget } = useDragAndDrop((event, data) => emit(event, data))

let ctrRef = ref(null)
let containerHeight = ref(0)
let showCtxMenu = ref(false)
let activeCtxNode = ref(null)
let activeCtxItemOpts = ref([])
let hoveredNode = ref(null)

const activeQueryTab = computed(() => QueryTab.find(props.activeQueryTabId) || {})
const activeQueryTabType = computed(() => typy(activeQueryTab.value, 'type').safeString)
const alterEditor = computed(() => AlterEditor.find(activeQueryTab.value.id))
const queryTabTmp = computed(() => QueryTabTmp.find(activeQueryTab.value.id))
const insightViewer = computed(() => InsightViewer.find(activeQueryTab.value.id))
const dbTreeData = computed(() => typy(props.queryEditorTmp, 'db_tree').safeArray)
const baseOptsMap = computed(() => {
  const previewOpts = [
    { title: t('previewData'), type: PRVW_DATA },
    { title: t('viewDetails'), type: PRVW_DATA_DETAILS },
  ]
  const spFnTriggerOpts = [
    { title: t('showCreate'), type: VIEW_INSIGHTS },
    { divider: true },
    ...TXT_OPS,
  ]
  return {
    [SCHEMA]: [
      { title: t('useDb'), type: USE },
      { title: t('viewInsights'), type: VIEW_INSIGHTS },
      { title: t('genErd'), type: GEN_ERD },
      ...TXT_OPS,
    ],
    [TBL]: [
      ...previewOpts,
      { title: t('viewInsights'), type: VIEW_INSIGHTS },
      { divider: true },
      ...TXT_OPS,
    ],
    [VIEW]: [
      ...previewOpts,
      { title: t('showCreate'), type: VIEW_INSIGHTS },
      { divider: true },
      ...TXT_OPS,
    ],
    [SP]: spFnTriggerOpts,
    [FN]: spFnTriggerOpts,
    [COL]: TXT_OPS,
    [IDX]: TXT_OPS,
    [TRIGGER]: spFnTriggerOpts,
  }
})

let expandedNodes = computed({
  get: () => typy(props.schemaSidebar, 'expanded_nodes').safeArray,
  set: (v) =>
    SchemaSidebar.update({
      where: props.queryEditorId,
      // Sort nodes by level as the order is important which is used to reload the schema and update the tree
      data: { expanded_nodes: v.map(minimizeNode).sort((a, b) => a.level - b.level) },
    }),
})
let activeNode = computed({
  get: () => {
    switch (activeQueryTabType.value) {
      case ALTER_EDITOR:
        return typy(alterEditor.value, 'active_node').safeObjectOrEmpty
      case INSIGHT_VIEWER:
        return typy(insightViewer.value, 'active_node').safeObjectOrEmpty
      case SQL_EDITOR:
        return typy(queryTabTmp.value, 'previewing_node').safeObjectOrEmpty
      default:
        return null
    }
  },
  set: (node) => {
    const minimizedNode = minimizeNode(node)
    switch (activeQueryTabType.value) {
      case ALTER_EDITOR:
        AlterEditor.update({
          where: props.activeQueryTabId,
          data: { active_node: minimizedNode },
        })
        break
      case INSIGHT_VIEWER:
        InsightViewer.update({
          where: props.activeQueryTabId,
          data: { active_node: minimizedNode },
        })
        break
      case SQL_EDITOR:
        QueryTabTmp.update({
          where: props.activeQueryTabId,
          data: { previewing_node: minimizedNode },
        })
        break
    }
  },
})

watch(showCtxMenu, (v) => {
  if (!v) activeCtxNode.value = null
})

onMounted(() => nextTick(() => setCtrHeight()))

function setCtrHeight() {
  const { height } = ctrRef.value.getBoundingClientRect()
  containerHeight.value = height
}

function showCtxBtn(node) {
  return Boolean(activeCtxNode.value && node.id === activeCtxNode.value.id)
}

/**
 * @param {Array} node - a node in db_tree_map
 * @returns {Array} minimized node
 */
const minimizeNode = ({ id, level, name, parentNameData, qualified_name, type }) => ({
  id,
  level,
  name,
  parentNameData,
  qualified_name,
  type,
})

/**
 * @param {Object} node - a node in db_tree_map
 * @returns {Array} context options for non system node
 */
function genUserNodeOpts(node) {
  const label = capitalizeFirstLetter(node.type.toLowerCase())

  const dropOpt = { title: `${DROP} ${label}`, type: DROP }
  const alterOpt = { title: `${ALTER} ${label}`, type: ALTER }
  const truncateOpt = { title: `${TRUNCATE} ${label}`, type: TRUNCATE }

  switch (node.type) {
    case SCHEMA:
    case VIEW:
    case SP:
    case FN:
    case TRIGGER:
      return [dropOpt]
    case TBL:
      return [alterOpt, dropOpt, truncateOpt]
    case IDX:
      return [dropOpt]
    case COL:
    default:
      return []
  }
}

/**
 * Both INSERT and CLIPBOARD types have same options.
 * This generates txt options based on provided type
 * @param {String} type - INSERT OR CLIPBOARD
 * @returns {Array} - return context options
 */
function genTxtOpts(type) {
  return [
    { title: t('qualifiedName'), type },
    { title: t('nameQuoted'), type },
    { title: t('name'), type },
  ]
}

function genNodeOpts(node) {
  const baseOpts = baseOptsMap.value[node.type]
  let opts = baseOpts
  if (node.isSys) return opts
  const userNodeOpts = genUserNodeOpts(node)
  if (userNodeOpts.length) opts = [...opts, { divider: true }, ...userNodeOpts]
  return opts
}

/**
 * Both INSERT and CLIPBOARD types have same options.
 * This handles INSERT and CLIPBOARD options
 * @param {Object} node - node
 * @param {Object} opt - context menu option
 */
function handleTxtOpt({ node, opt }) {
  let v = ''
  switch (opt.title) {
    case t('qualifiedName'):
      v = node.qualified_name
      break
    case t('nameQuoted'):
      v = quotingIdentifier(node.name)
      break
    case t('name'):
      v = node.name
      break
  }
  const { INSERT, CLIPBOARD } = NODE_CTX_TYPES
  switch (opt.type) {
    case INSERT:
      emit('place-to-editor', v)
      break
    case CLIPBOARD:
      copyTextToClipboard(v)
      break
  }
}

function handleOpenCtxMenu(node) {
  if (isEqual(activeCtxNode.value, node)) {
    showCtxMenu.value = false
    activeCtxNode.value = null
  } else {
    if (!showCtxMenu.value) showCtxMenu.value = true
    activeCtxNode.value = node
    activeCtxItemOpts.value = genNodeOpts(node)
  }
}

function previewNode(node) {
  activeNode.value = node
  emit('get-node-data', {
    query_mode: QUERY_MODES.PRVW_DATA,
    qualified_name: node.qualified_name,
  })
}

function onNodeDblClick(node) {
  if (node.type === SCHEMA) emit('use-db', node.qualified_name)
}

function onContextMenu(node) {
  if (NODES_HAVE_CTX_MENU.includes(node.type)) handleOpenCtxMenu(node)
}

function onNodeDragStart(e) {
  e.preventDefault()
  isDragging.value = true
  dragTarget.value = e.target
}

/**
 * @param {Object} node - node
 * @param {Object} opt - context menu option
 */
function optionHandler({ node, opt }) {
  const {
    PRVW_DATA,
    PRVW_DATA_DETAILS,
    USE,
    INSERT,
    CLIPBOARD,
    DROP,
    ALTER,
    TRUNCATE,
    GEN_ERD,
    VIEW_INSIGHTS,
  } = NODE_CTX_TYPES

  switch (opt.type) {
    case USE:
      emit('use-db', node.qualified_name)
      break
    case PRVW_DATA:
    case PRVW_DATA_DETAILS:
      activeNode.value = node
      emit('get-node-data', {
        query_mode: opt.type,
        qualified_name: node.qualified_name,
      })
      break
    case INSERT:
    case CLIPBOARD:
      handleTxtOpt({ node, opt })
      break
    case DROP: {
      let sql = `DROP ${node.type} ${node.qualified_name};`
      if (node.type === IDX) {
        const db = schemaNodeHelper.getSchemaName(node)
        const tbl = schemaNodeHelper.getTblName(node)
        const target = `${quotingIdentifier(db)}.${quotingIdentifier(tbl)}`
        sql = `DROP ${node.type} ${quotingIdentifier(node.name)} ON ${target};`
      }
      emit('drop-action', sql)
      break
    }
    case ALTER:
      if (node.type === TBL) emit('alter-tbl', minimizeNode(node))
      break
    case TRUNCATE:
      if (node.type === TBL) emit('truncate-tbl', `TRUNCATE TABLE ${node.qualified_name};`)
      break
    case GEN_ERD:
      emit('gen-erd', minimizeNode(node))
      break
    case VIEW_INSIGHTS:
      emit('view-node-insights', minimizeNode(node))
      break
  }
}

function onTreeChanges(tree) {
  QueryEditorTmp.update({ where: props.queryEditorId, data: { db_tree: tree } })
}
</script>

<template>
  <div ref="ctrRef" class="schema-tree-ctr fill-height" v-resize.quiet="setCtrHeight">
    <VirSchemaTree
      :data="dbTreeData"
      v-model:expandedNodes="expandedNodes"
      :loadChildren="loadChildren"
      :height="containerHeight"
      :search="filterTxt"
      class="vir-schema-tree"
      hasNodeCtxEvt
      hasDbClickEvt
      :activeNode="activeNode"
      @on-tree-changes="onTreeChanges"
      @node:contextmenu="onContextMenu"
      @node:dblclick="onNodeDblClick"
      v-bind="$attrs"
    >
      <template #label="{ node, isHovering }">
        <div class="node-content d-flex align-center w-100 fill-height">
          <div
            class="d-flex align-center node__label fill-height"
            @mouseenter="hoveredNode = node"
            @mouseleave="hoveredNode = null"
          >
            <SchemaNodeIcon class="mr-1" :node="node" :size="12" />
            <span
              v-mxs-highlighter="{ keyword: filterTxt, txt: node.name }"
              class="text-truncate d-inline-block node-name"
              :class="{
                'font-weight-bold':
                  node.type === SCHEMA && activeQueryTabConn.active_db === node.qualified_name,
                'cursor--grab': node.draggable,
              }"
              @mousedown="node.draggable ? onNodeDragStart($event) : null"
            >
              {{ node.name }}
            </span>
            <span class="text-truncate d-inline-block grayed-out-info ml-1">
              <template v-if="$typy(node, 'data.COLUMN_TYPE').safeString">
                {{ $typy(node, 'data.COLUMN_TYPE').safeString }}
              </template>
              <template v-if="node.type === IDX && $typy(node, 'data.COLUMN_NAME').safeString">
                {{ $typy(node, 'data.COLUMN_NAME').safeString }}
              </template>
            </span>
          </div>
          <div class="d-flex align-center node__append ml-1">
            <VBtn
              v-if="node.type === NODE_TYPES.TBL || node.type === NODE_TYPES.VIEW"
              v-show="isHovering"
              :id="`prvw-btn-tooltip-activator-${node.key}`"
              variant="text"
              density="compact"
              size="small"
              icon
              class="mr-1"
              @click.stop="previewNode(node)"
            >
              <VIcon size="14" color="primary" icon="$mdiTableEye" />
            </VBtn>
            <VBtn
              v-show="NODES_HAVE_CTX_MENU.includes(node.type) && (isHovering || showCtxBtn(node))"
              :id="`ctx-menu-activator-${node.key}`"
              variant="text"
              density="compact"
              size="small"
              icon
              @click.stop="handleOpenCtxMenu(node)"
            >
              <VIcon size="14" color="primary" icon="$mdiDotsHorizontal" />
            </VBtn>
          </div>
        </div>
      </template>
    </VirSchemaTree>
    <VTooltip
      v-if="hoveredNode"
      location="top"
      class="preview-data-tooltip"
      :activator="`#prvw-btn-tooltip-activator-${hoveredNode.key}`"
    >
      {{ t('previewData') }}
    </VTooltip>
    <VTooltip
      v-if="hoveredNode && NODES_HAVE_CTX_MENU.includes(hoveredNode.type)"
      :key="hoveredNode.key"
      :model-value="Boolean(hoveredNode)"
      :disabled="isDragging"
      location="right"
      offset="0"
      transition="fade-transition"
      :activator="`#node-${hoveredNode.key}`"
    >
      <table class="node-tooltip">
        <tbody>
          <tr v-for="(value, key) in hoveredNode.data" :key="key">
            <td class="font-weight-bold pr-2">{{ key }}:</td>
            <td>{{ value }}</td>
          </tr>
        </tbody>
      </table>
    </VTooltip>
    <CtxMenu
      v-if="activeCtxNode"
      :key="activeCtxNode.key"
      v-model="showCtxMenu"
      :items="activeCtxItemOpts"
      :activator="`#ctx-menu-activator-${activeCtxNode.key}`"
      location="bottom end"
      offset="4 8"
      transition="slide-y-transition"
      @item-click="optionHandler({ node: activeCtxNode, opt: $event })"
    />
  </div>
</template>

<style lang="scss" scoped>
.node-tooltip {
  font-size: 0.75rem;
}
.vir-schema-tree {
  .node-content {
    flex-basis: 0%;
    flex-grow: 1;
    flex-shrink: 0;
    min-width: 0;
    .node__label {
      flex: 1;
      overflow: hidden;
      text-overflow: ellipsis;
      white-space: nowrap;
    }
    .node__append {
      min-width: 20px;
    }
  }
}
</style>
