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
import QueryEditorTmp from '@wsModels/QueryEditorTmp'
import InsightViewer from '@wsModels/InsightViewer'
import TblEditor from '@wsModels/TblEditor'
import DdlEditor from '@wsModels/DdlEditor'
import QueryTab from '@wsModels/QueryTab'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import SchemaSidebar from '@wsModels/SchemaSidebar'
import VirSchemaTree from '@wsComps/VirSchemaTree.vue'
import SchemaNodeIcon from '@wsComps/SchemaNodeIcon.vue'
import schemaNodeHelper from '@/utils/schemaNodeHelper'
import {
  NODE_TYPE_MAP,
  ALL_NODE_TYPES,
  QUERY_MODE_MAP,
  NODE_CTX_TYPE_MAP,
  QUERY_TAB_TYPE_MAP,
  TABLE_STRUCTURE_SPEC_MAP,
} from '@/constants/workspace'

defineOptions({ inheritAttrs: false })
const props = defineProps({
  queryEditorId: { type: String, required: true },
  activeQueryTabId: { type: String, required: true },
  queryEditorTmp: { type: Object, required: true },
  activeQueryTabConn: { type: Object, required: true },
  schemaSidebar: { type: Object, required: true },
})
const emit = defineEmits([
  'place-to-editor', //  v:string. Place text to editor
  'get-node-data', // { mode: string, node: object }
  'use-db', // v:string. qualified_name
  'drop-action', // v:string. sql
  'truncate-tbl', // v:string. sql
  'gen-erd', // v:object. Schema node
  'view-node-insights', // v:object. Either Schema or Table node.
  'on-dragging', // v:object. Event emitted from useDragAndDrop
  'on-dragend', // v:object. Event emitted from useDragAndDrop
  'create-node', // { type: string, parentNameData: object }
  'alter-node', // { node: object, spec?: string }
])

const typy = useTypy()
const { t } = useI18n()
const {
  quotingIdentifier,
  copyTextToClipboard,
  lodash: { isEqual },
} = useHelpers()

const NON_SYS_NODE_OPT_MAP = Object.freeze(
  ALL_NODE_TYPES.reduce((map, type) => {
    map[type] = schemaNodeHelper.genNonSysNodeOpts(type)
    return map
  }, {})
)
const { TBL_EDITOR, INSIGHT_VIEWER, SQL_EDITOR, DDL_EDITOR } = QUERY_TAB_TYPE_MAP
const { SCHEMA, TBL, VIEW, SP, FN, COL, IDX, TRIGGER } = NODE_TYPE_MAP
const {
  USE,
  VIEW_INSIGHTS,
  PRVW_DATA,
  PRVW_DATA_DETAILS,
  GEN_ERD,
  INSERT,
  CLIPBOARD,
  DROP,
  ALTER,
  TRUNCATE,
  CREATE,
  ADD,
} = NODE_CTX_TYPE_MAP

const TXT_OPS = [
  schemaNodeHelper.genNodeOpt({ title: t('placeToEditor'), children: genTxtOpts(INSERT) }),
  schemaNodeHelper.genNodeOpt({ title: t('copyToClipboard'), children: genTxtOpts(CLIPBOARD) }),
]
const SP_FN_TRIGGER_OPTS = [
  schemaNodeHelper.genNodeOpt({ title: t('showCreate'), type: VIEW_INSIGHTS }),
  { divider: true },
  ...TXT_OPS,
]
const PRVW_OPTS = [
  schemaNodeHelper.genNodeOpt({ title: t('previewData'), type: PRVW_DATA, targetNodeType: TBL }),
  schemaNodeHelper.genNodeOpt({
    title: t('viewDetails'),
    type: PRVW_DATA_DETAILS,
    targetNodeType: TBL,
  }),
]
const BASE_OPT_MAP = {
  [SCHEMA]: [
    schemaNodeHelper.genNodeOpt({ title: t('useDb'), type: USE, targetNodeType: SCHEMA }),
    schemaNodeHelper.genNodeOpt({
      title: t('viewInsights'),
      type: VIEW_INSIGHTS,
      targetNodeType: SCHEMA,
    }),
    schemaNodeHelper.genNodeOpt({ title: t('genErd'), type: GEN_ERD, targetNodeType: SCHEMA }),
    ...TXT_OPS,
  ],
  [TBL]: [
    ...PRVW_OPTS,
    schemaNodeHelper.genNodeOpt({
      title: t('viewInsights'),
      type: VIEW_INSIGHTS,
      targetNodeType: TBL,
    }),
    { divider: true },
    ...TXT_OPS,
  ],
  [VIEW]: [
    ...PRVW_OPTS,
    schemaNodeHelper.genNodeOpt({
      title: t('showCreate'),
      type: VIEW_INSIGHTS,
      targetNodeType: VIEW,
    }),
    { divider: true },
    ...TXT_OPS,
  ],
  [SP]: SP_FN_TRIGGER_OPTS,
  [FN]: SP_FN_TRIGGER_OPTS,
  [COL]: TXT_OPS,
  [IDX]: TXT_OPS,
  [TRIGGER]: SP_FN_TRIGGER_OPTS,
}

const { isDragging, dragTarget } = useDragAndDrop((event, data) => emit(event, data))

const virSchemaTreeRef = ref(null)
const showCtxMenu = ref(false)
const activeCtxNode = ref(null)
const activeCtxItemOpts = ref([])
const hoveredNode = ref(null)

const activeQueryTab = computed(() => QueryTab.find(props.activeQueryTabId) || {})
const activeQueryTabType = computed(() => typy(activeQueryTab.value, 'type').safeString)
const tblEditor = computed(() => TblEditor.find(activeQueryTab.value.id))
const queryTabTmp = computed(() => QueryTabTmp.find(activeQueryTab.value.id))
const insightViewer = computed(() => InsightViewer.find(activeQueryTab.value.id))
const ddlEditor = computed(() => DdlEditor.find(activeQueryTab.value.id))
const dbTreeData = computed(() => typy(props.queryEditorTmp, 'db_tree').safeArray)

const expandedNodes = computed({
  get: () => typy(props.schemaSidebar, 'expanded_nodes').safeArray,
  set: (v) =>
    SchemaSidebar.update({
      where: props.queryEditorId,
      // Sort nodes by level as the order is important which is used to reload the schema and update the tree
      data: {
        expanded_nodes: v.map(schemaNodeHelper.minimizeNode).sort((a, b) => a.level - b.level),
      },
    }),
})
const activeNode = computed(() => {
  let modelData
  switch (activeQueryTabType.value) {
    case TBL_EDITOR:
      modelData = tblEditor.value
      break
    case INSIGHT_VIEWER:
      modelData = insightViewer.value
      break
    case DDL_EDITOR:
      modelData = ddlEditor.value
      break
    case SQL_EDITOR:
      modelData = queryTabTmp.value
      break
  }
  return typy(modelData, 'active_node').safeObjectOrEmpty
})
const hoveredNodeKey = computed(() => typy(hoveredNode.value, 'key').safeString)

watch(showCtxMenu, (v) => {
  if (!v) activeCtxNode.value = null
})

function showCtxBtn(node) {
  return Boolean(activeCtxNode.value && node.id === activeCtxNode.value.id)
}

/**
 * Both INSERT and CLIPBOARD types have same options.
 * This generates txt options based on provided type
 * @param {String} type - INSERT OR CLIPBOARD
 * @returns {Array} - return context options
 */
function genTxtOpts(type) {
  return [
    schemaNodeHelper.genNodeOpt({ title: t('qualifiedName'), type }),
    schemaNodeHelper.genNodeOpt({ title: t('nameQuoted'), type }),
    schemaNodeHelper.genNodeOpt({ title: t('name'), type }),
  ]
}

function genNodeOpts(node) {
  const baseOpts = BASE_OPT_MAP[node.type] || []
  if (node.isSys) return baseOpts
  const nodeOpts = NON_SYS_NODE_OPT_MAP[node.type]
  if (nodeOpts.length) {
    let opts = []
    if (baseOpts.length) opts.push(...baseOpts, { divider: true })
    return [...opts, ...nodeOpts]
  }
  return baseOpts
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

function previewNode({ mode, node }) {
  emit('get-node-data', { mode, node: schemaNodeHelper.minimizeNode(node) })
}

function autoExpandSchemaNode(node) {
  if (!typy(virSchemaTreeRef.value, 'isExpanded').safeFunction(node))
    typy(virSchemaTreeRef.value, 'toggleNode').safeFunction(node)
}

function emitUseDb(node) {
  emit('use-db', node.qualified_name)
  autoExpandSchemaNode(node)
}

function onNodeDblClick(node) {
  if (node.type === SCHEMA) emitUseDb(node)
}

function onNodeRightClick(node) {
  handleOpenCtxMenu(node)
}

function onNodeDragStart(e) {
  e.preventDefault()
  isDragging.value = true
  dragTarget.value = e.target
}

/**
 * @param param0
 */
function handleEmitAlterTbl({ node, targetNodeType }) {
  let spec = TABLE_STRUCTURE_SPEC_MAP.COLUMNS
  if (targetNodeType === IDX) spec = TABLE_STRUCTURE_SPEC_MAP.INDEXES
  const tblNode =
    node.type === TBL
      ? node
      : schemaNodeHelper.findNodeAncestor({
          tree: dbTreeData.value,
          nodeKey: node.key,
          ancestorType: TBL,
        })
  emit('alter-node', { node: tblNode, spec })
}

/**
 * @param {Object} node - node
 * @param {Object} opt - context menu option
 */
function optionHandler({ node, opt }) {
  const targetNodeType = opt.targetNodeType
  switch (opt.type) {
    case USE:
      emitUseDb(node)
      break
    case PRVW_DATA:
    case PRVW_DATA_DETAILS:
      previewNode({ mode: opt.type, node })
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
    case ADD:
      // Add column or index to an existing table will also be done via the TblEditor
      if (targetNodeType === TBL || targetNodeType === COL || targetNodeType === IDX)
        handleEmitAlterTbl({ node, targetNodeType })
      else if (opt.type === ALTER) emit('alter-node', { node })
      break
    case TRUNCATE:
      if (node.type === TBL) emit('truncate-tbl', `TRUNCATE TABLE ${node.qualified_name};`)
      break
    case GEN_ERD:
      emit('gen-erd', schemaNodeHelper.minimizeNode(node))
      break
    case VIEW_INSIGHTS:
      emit('view-node-insights', schemaNodeHelper.minimizeNode(node))
      break
    case CREATE:
      emit('create-node', { type: targetNodeType, parentNameData: node.parentNameData })
      break
  }
}

function onTreeChanges(tree) {
  QueryEditorTmp.update({ where: props.queryEditorId, data: { db_tree: tree } })
}
</script>

<template>
  <div class="schema-tree-ctr fill-height">
    <VirSchemaTree
      ref="virSchemaTreeRef"
      :data="dbTreeData"
      v-model:expandedNodes="expandedNodes"
      class="vir-schema-tree"
      hasNodeCtxEvt
      hasDbClickEvt
      :activeNode="activeNode"
      @on-tree-changes="onTreeChanges"
      @node:contextmenu="onNodeRightClick"
      @node:dblclick="onNodeDblClick"
      @node-hovered="hoveredNode = $event"
      v-bind="$attrs"
    >
      <template #label="{ node, isHovering }">
        <div class="node-content d-flex align-center w-100 fill-height">
          <div class="d-flex align-center node__label fill-height">
            <SchemaNodeIcon class="mr-1" :type="node.type" :attrs="node.data" :size="12" />
            <span
              v-mxs-highlighter="{ keyword: $attrs.search, txt: node.name }"
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
              v-if="node.type === TBL || node.type === VIEW"
              v-show="isHovering"
              :id="`prvw-btn-tooltip-activator-${node.key}`"
              variant="text"
              density="compact"
              size="small"
              icon
              class="mr-1"
              @click.stop="previewNode({ mode: QUERY_MODE_MAP.PRVW_DATA, node })"
            >
              <VIcon size="14" color="primary" icon="$mdiTableEye" />
            </VBtn>
            <VBtn
              v-show="isHovering || showCtxBtn(node)"
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
      location="top"
      :offset="0"
      :activator="`#prvw-btn-tooltip-activator-${hoveredNodeKey}`"
      data-test="preview-data-tooltip"
    >
      {{ t('previewData') }}
    </VTooltip>
    <VTooltip
      v-if="$typy(hoveredNode, 'data').isDefined"
      :disabled="isDragging"
      location="right"
      :offset="0"
      content-class="node-info"
      :activator="`#node-${hoveredNodeKey}`"
      data-test="node-tooltip"
    >
      <table>
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
.node-info {
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
