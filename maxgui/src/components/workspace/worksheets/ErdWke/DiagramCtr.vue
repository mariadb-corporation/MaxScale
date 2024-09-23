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
import ErdTask from '@wsModels/ErdTask'
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import Worksheet from '@wsModels/Worksheet'
import ErToolbar from '@wkeComps/ErdWke/ErToolbar.vue'
import EntityDiagram from '@wkeComps/ErdWke/EntityDiagram.vue'
import DiagramCtxMenu from '@wkeComps/ErdWke/DiagramCtxMenu.vue'
import erdTaskService from '@wsServices/erdTaskService'
import schemaInfoService from '@wsServices/schemaInfoService'
import { LINK_SHAPES } from '@/components/svgGraph/shapeConfig'
import { EVENT_TYPES } from '@/components/svgGraph/linkConfig'
import erdHelper from '@/utils/erdHelper'
import utils from '@wkeComps/ErdWke/diagramUtils'
import { DIAGRAM_CTX_TYPE_MAP, SNACKBAR_TYPE_MAP } from '@/constants'
import { TABLE_STRUCTURE_SPEC_MAP } from '@/constants/workspace'
import html2canvas from 'html2canvas'

const props = defineProps({
  disabled: { type: Boolean, required: true },
  dim: { type: Object, required: true },
  graphHeightPct: { type: Number, required: true },
  erdTask: { type: Object, required: true },
  conn: { type: Object, required: true },
  nodeMap: { type: Object, required: true },
  nodes: { type: Array, required: true },
  tables: { type: Array, required: true },
  schemas: { type: Array, required: true },
  activeEntityId: { type: String, required: true },
  erdTaskTmp: { type: Object, required: true },
  refTargetMap: { type: Object, required: true },
  tablesColNameMap: { type: Object, required: true },
  applyScript: { type: Function, required: true },
})
const emit = defineEmits(['on-copy-script-to-clipboard', 'on-export-script', 'on-export-as-jpeg'])

const store = useStore()
const typy = useTypy()
const { t } = useI18n()
const {
  lodash: { merge, keyBy },
  immutableUpdate,
} = useHelpers()
const { data: ctxMenuData, openCtxMenu } = useCtxMenu()
const zoomAndPanController = useZoomAndPanController()
const { panAndZoom, isFitIntoView } = zoomAndPanController

const { NODE } = DIAGRAM_CTX_TYPE_MAP
const TOOLBAR_HEIGHT = 40
const SCALE_EXTENT = [0.25, 2]
const ERD_EXPORT_OPTS = [
  { title: t('copyScriptToClipboard'), action: () => emit('on-copy-script-to-clipboard') },
  { title: t('exportScript'), action: () => emit('on-export-script') },
  { title: t('exportAsJpeg'), action: () => emit('on-export-as-jpeg') },
]

const entityDiagramRef = ref(null)
const graphConfigData = ref({
  link: {
    color: '#424f62',
    strokeWidth: 1,
    isAttrToAttr: false,
    isHighlightAll: false,
    opacity: 1,
    [EVENT_TYPES.HOVER]: { color: 'white', invisibleOpacity: 1 },
    [EVENT_TYPES.DRAGGING]: { color: 'white', invisibleOpacity: 1 },
  },
  marker: { width: 18 },
  linkShape: {
    type: LINK_SHAPES.ORTHO,
    entitySizeConfig: { rowHeight: 32, rowOffset: 4, headerHeight: 32 },
  },
})

const charset_collation_map = computed(() => store.state.schemaInfo.charset_collation_map)
const activeRequestConfig = computed(() => Worksheet.getters('activeRequestConfig'))

const connId = computed(() => typy(props.conn, 'id').safeString)
const activeGraphConfig = computed(() => typy(props.erdTask, 'graph_config').safeObjectOrEmpty)
const isLaidOut = computed(() => typy(props.erdTask, 'is_laid_out').safeBoolean)
const diagramDim = computed(() => ({
  width: props.dim.width,
  height: props.dim.height - TOOLBAR_HEIGHT,
}))
const colKeyCategoryMap = computed(() =>
  props.tables.reduce((map, tbl) => {
    map = { ...map, ...erdHelper.genColKeyTypeMap(tbl.defs.key_category_map) }
    return map
  }, {})
)
const nodesHistory = computed(() => typy(props.erdTaskTmp, 'nodes_history').safeArray)
const activeHistoryIdx = computed(() => typy(props.erdTaskTmp, 'active_history_idx').safeNumber)

watch(
  graphConfigData,
  (v) => {
    ErdTask.update({
      where: props.erdTask.id,
      data: {
        graph_config: immutableUpdate(activeGraphConfig.value, {
          link: {
            isAttrToAttr: { $set: v.link.isAttrToAttr },
            isHighlightAll: { $set: v.link.isHighlightAll },
          },
          linkShape: {
            type: { $set: v.linkShape.type },
          },
        }),
      },
    })
  },
  { deep: true }
)
watch(
  () => props.activeEntityId,
  (v) => {
    if (!v) fitIntoView()
  }
)

onBeforeMount(() => (graphConfigData.value = merge(graphConfigData.value, activeGraphConfig.value)))

/**
 * @public
 */
function updateNode(params) {
  entityDiagramRef.value.updateNode(params)
}

/**
 * @public
 * @returns {Promise<Canvas>}
 */
async function getCanvas() {
  return await html2canvas(entityDiagramRef.value.$el, { logging: false })
}

function onRendered(diagram) {
  onNodesCoordsUpdate(diagram.nodes)
  if (diagram.nodes.length) fitIntoView()
}

/**
 * Update new nodeMap data and redrawn nodes
 * @param {object} param
 * @param {object} param.nodeMap - new node map
 * @param {boolean} param.skipHistory - conditionally skip nodes history update
 */
function redraw({ nodeMap, skipHistory }) {
  ErdTask.update({ where: props.erdTask.id, data: { nodeMap } }).then(() => {
    entityDiagramRef.value.update(props.nodes)
    if (!skipHistory) erdTaskService.updateNodesHistory(nodeMap)
  })
}

/**
 * @param {array} v - diagram staging nodes with new coordinate values
 */
function onNodesCoordsUpdate(v) {
  const nodeMap = utils.assignCoord({ nodes: props.nodes, nodeMap: keyBy(v, 'id') })
  ErdTask.update({
    where: props.erdTask.id,
    data: { nodeMap, is_laid_out: true },
  })
  erdTaskService.updateNodesHistory(nodeMap)
}

/**
 * @param {object} node - node with new coordinates
 */
function onNodeDragEnd(node) {
  onNodesCoordsUpdate([node])
}

/**
 * Auto adjust (zoom in or out) the contents of a graph
 * @param {number} [param.v] - zoom value
 * @param {boolean} [param.isFitIntoView] - if it's true, v param will be ignored
 * @param {object} [param.extent]
 * @param {number} [param.paddingPct]
 */
function zoomTo({ v, isFitIntoView = false, extent, paddingPct }) {
  zoomAndPanController.zoomTo({
    v,
    isFitIntoView,
    extent: extent ? extent : entityDiagramRef.value.getExtent(),
    scaleExtent: SCALE_EXTENT,
    dim: diagramDim.value,
    paddingPct,
  })
}

function zoomIntoNode(node) {
  const minX = node.x - node.size.width / 2
  const minY = node.y - node.size.height / 2
  const maxX = minX + node.size.width
  const maxY = minY + node.size.height
  zoomTo({
    isFitIntoView: true,
    extent: { minX, maxX, minY, maxY },
    /* add a padding of 20%, so there'd be some reserved space if the users
     * alter the table by adding new column
     */
    paddingPct: 20,
  })
}

function fitIntoView() {
  zoomTo({ isFitIntoView: true })
}

async function navHistory(idx) {
  await erdTaskService.updateActiveHistoryIdx(idx)
  redraw({ nodeMap: nodesHistory.value[activeHistoryIdx.value], skipHistory: true })
}

function autoArrange() {
  ErdTask.update({
    where: props.erdTask.id,
    data: { is_laid_out: false },
  }).then(() => entityDiagramRef.value.runSimulation((diagram) => onRendered(diagram)))
}

function patchGraphConfig({ path, value }) {
  graphConfigData.value = utils.immutableUpdateConfig(graphConfigData.value, path, value)
}

function handleOpenEditor({ node, spec = TABLE_STRUCTURE_SPEC_MAP.COLUMNS, skipZoom = false }) {
  if (connId.value) {
    const data = { active_entity_id: node.id, active_spec: spec }
    if (props.graphHeightPct === 100) data.graph_height_pct = 40
    ErdTaskTmp.update({ where: props.erdTask.id, data }).then(() => {
      if (!skipZoom) zoomIntoNode(node)
    })
  } else
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
      text: [t('errors.requiredConn')],
      type: SNACKBAR_TYPE_MAP.ERROR,
    })
}

/**
 * Add a new table node to nodeMap
 */
async function addTblNode() {
  if (connId.value) {
    await schemaInfoService.querySuppData({
      connId: connId.value,
      config: activeRequestConfig.value,
    })
    const node = utils.genTblNode({
      nodes: props.nodes,
      schemas: props.schemas,
      charsetCollationMap: charset_collation_map.value,
      panAndZoom: panAndZoom.value,
    })
    const nodeMap = immutableUpdate(props.nodeMap, { $merge: { [node.id]: node } })
    ErdTask.update({
      where: props.erdTask.id,
      data: { nodeMap },
    }).then(() => {
      erdTaskService.updateNodesHistory(nodeMap)
      entityDiagramRef.value.addNode(node)
      handleOpenEditor({ node, skipZoom: true })
    })
  } else
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
      text: [t('errors.requiredConn')],
      type: SNACKBAR_TYPE_MAP.ERROR,
    })
}

function handleRmTblNode(node) {
  const nodeMap = utils.rmTblNode({ id: node.id, nodes: props.nodes })
  // Close editor by clearing active_entity_id
  ErdTaskTmp.update({
    where: props.erdTask.id,
    data: { active_entity_id: '', graph_height_pct: 100 },
  })
  redraw({ nodeMap })
}

function handleAddFk(param) {
  const nodeMap = utils.addFk({
    nodeMap: props.nodeMap,
    colKeyCategoryMap: colKeyCategoryMap.value,
    ...param,
  })
  if (nodeMap) redraw({ nodeMap })
  else
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
      text: [t('errors.fkColsRequirements')],
      type: SNACKBAR_TYPE_MAP.ERROR,
    })
}

function handleRmFk(link) {
  const nodeMap = utils.rmFk({ nodeMap: props.nodeMap, link })
  redraw({ nodeMap })
}

function handleUpdateCardinality({ type, link }) {
  const nodeMap = utils.updateCardinality({ nodeMap: props.nodeMap, type, link })
  redraw({ nodeMap })
}

defineExpose({ updateNode, getCanvas })
</script>

<template>
  <div class="fill-height d-flex flex-column">
    <ErToolbar
      :graphConfig="graphConfigData"
      :height="TOOLBAR_HEIGHT"
      :zoom="panAndZoom.k"
      :isFitIntoView="isFitIntoView"
      :exportOptions="ERD_EXPORT_OPTS"
      :conn="conn"
      :nodesHistory="nodesHistory"
      :activeHistoryIdx="activeHistoryIdx"
      @set-zoom="zoomTo"
      @on-create-table="addTblNode"
      @on-undo="navHistory(activeHistoryIdx - 1)"
      @on-redo="navHistory(activeHistoryIdx + 1)"
      @click-auto-arrange="autoArrange"
      @change-graph-config-attr-value="patchGraphConfig"
      @on-apply-script="applyScript"
    />
    <EntityDiagram
      ref="entityDiagramRef"
      v-model:panAndZoom="panAndZoom"
      :nodes="nodes"
      :dim="diagramDim"
      :scaleExtent="SCALE_EXTENT"
      :graphConfigData="graphConfigData"
      :isLaidOut="isLaidOut"
      :activeNodeId="activeEntityId"
      :refTargetMap="refTargetMap"
      :tablesColNameMap="tablesColNameMap"
      :colKeyCategoryMap="colKeyCategoryMap"
      class="entity-diagram"
      @on-rendered.once="onRendered($event)"
      @on-node-drag-end="onNodeDragEnd($event)"
      @dblclick="disabled ? null : handleOpenEditor({ node: $event })"
      @on-create-new-fk="handleAddFk($event)"
      @contextmenu="openCtxMenu($event)"
    >
      <template #entity-setting-btn="{ node, isHovering }">
        <VBtn
          :id="node.id"
          size="small"
          class="setting-btn"
          :class="{ 'setting-btn--visible': isHovering || ctxMenuData.activatorId === node.id }"
          icon
          variant="text"
          density="compact"
          color="primary"
          :disabled="disabled"
          @click.stop="openCtxMenu({ e: $event, type: NODE, item: node })"
        >
          <VIcon size="14" icon="mxs:settings" />
        </VBtn>
      </template>
    </EntityDiagram>
    <DiagramCtxMenu
      v-model="ctxMenuData.isOpened"
      :data="ctxMenuData"
      :graphConfig="graphConfigData"
      :exportOptions="ERD_EXPORT_OPTS"
      :colKeyCategoryMap="colKeyCategoryMap"
      @create-tbl="addTblNode($event)"
      @fit-into-view="fitIntoView()"
      @auto-arrange-erd="autoArrange($event)"
      @patch-graph-config="patchGraphConfig($event)"
      @open-editor="handleOpenEditor($event)"
      @rm-tbl="handleRmTblNode($event)"
      @rm-fk="handleRmFk($event)"
      @update-cardinality="handleUpdateCardinality($event)"
    />
  </div>
</template>

<style lang="scss" scoped>
.entity-diagram {
  .setting-btn {
    visibility: hidden;
    .v-icon {
      transition: none;
    }
    &--visible {
      visibility: visible;
    }
  }
}
</style>
