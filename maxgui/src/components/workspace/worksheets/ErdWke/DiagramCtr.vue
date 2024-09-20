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
import ErToolbar from '@wkeComps/ErdWke/ErToolbar.vue'
import EntityDiagram from '@wkeComps/ErdWke/EntityDiagram.vue'
import DiagramCtxMenu from '@wkeComps/ErdWke/DiagramCtxMenu.vue'
import erdTaskService from '@wsServices/erdTaskService'
import { LINK_SHAPES } from '@/components/svgGraph/shapeConfig'
import { EVENT_TYPES } from '@/components/svgGraph/linkConfig'
import ddlTemplate from '@/utils/ddlTemplate'
import erdHelper from '@/utils/erdHelper'
import TableParser from '@/utils/TableParser'
import diagramUtils from '@wkeComps/ErdWke/diagramUtils'
import { DIAGRAM_CTX_TYPE_MAP, SNACKBAR_TYPE_MAP } from '@/constants'
import {
  LINK_OPT_TYPE_MAP,
  TABLE_STRUCTURE_SPEC_MAP,
  CREATE_TBL_TOKEN_MAP,
} from '@/constants/workspace'
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
  lodash: { merge, keyBy, cloneDeep, update },
  immutableUpdate,
  dynamicColors,
  uuidv1,
  getPanAndZoomValues,
} = useHelpers()
const { data: ctxMenuData, openCtxMenu } = useCtxMenu()

const { BOARD, NODE, LINK } = DIAGRAM_CTX_TYPE_MAP
const {
  SET_ONE_TO_ONE,
  SET_ONE_TO_MANY,
  SET_MANDATORY,
  SET_FK_COL_OPTIONAL,
  SET_REF_COL_MANDATORY,
  SET_REF_COL_OPTIONAL,
} = LINK_OPT_TYPE_MAP
const TOOLBAR_HEIGHT = 40
const SCALE_EXTENT = [0.25, 2]
const ERD_EXPORT_OPTS = [
  { title: t('copyScriptToClipboard'), action: () => emit('on-copy-script-to-clipboard') },
  { title: t('exportScript'), action: () => emit('on-export-script') },
  { title: t('exportAsJpeg'), action: () => emit('on-export-as-jpeg') },
]
const DIAGRAM_ID = `erd_${uuidv1()}`

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
const isFitIntoView = ref(false)
const panAndZoom = ref({ x: 0, y: 0, k: 1 })

const charset_collation_map = computed(() => store.state.schemaInfo.charset_collation_map)

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
  panAndZoom,
  (v) => {
    if (v.eventType && v.eventType == 'wheel') isFitIntoView.value = false
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

function onRendered(diagram) {
  onNodesCoordsUpdate(diagram.nodes)
  if (diagram.nodes.length) fitIntoView()
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

function rmTbl(node) {
  const nodeMap = props.nodes.reduce((map, n) => {
    if (n.id !== node.id) {
      const fkMap = n.data.defs.key_category_map[CREATE_TBL_TOKEN_MAP.foreignKey]
      if (!fkMap) map[n.id] = n
      else {
        const updatedFkMap = Object.values(fkMap).reduce((res, key) => {
          if (key.ref_tbl_id !== node.id) res[key.id] = key
          return res
        }, {})
        map[n.id] = immutableUpdate(n, {
          data: {
            defs: {
              key_category_map: Object.keys(updatedFkMap).length
                ? { $merge: { [CREATE_TBL_TOKEN_MAP.foreignKey]: updatedFkMap } }
                : { $unset: [CREATE_TBL_TOKEN_MAP.foreignKey] },
            },
          },
        })
      }
    }
    return map
  }, {})
  closeEditor()
  updateAndDrawNodes({ nodeMap })
}

function updateCardinality({ type, link }) {
  let nodeMap = props.nodeMap
  const { src_attr_id, target_attr_id } = link.relationshipData
  let method,
    nodeId = link.source.id,
    node = props.nodeMap[nodeId],
    colId = src_attr_id,
    value = false
  switch (type) {
    case SET_ONE_TO_MANY:
    case SET_ONE_TO_ONE: {
      method = diagramUtils.toggleUnique
      /**
       * In an one to many relationship, FK is placed on the "many" side,
       * and the FK col can't be unique. On the other hand, one to one
       * relationship, fk col and ref col must be both unique
       */
      if (type === SET_ONE_TO_ONE) {
        value = true
        // update also ref col of target node
        nodeMap = immutableUpdate(nodeMap, {
          [link.target.id]: {
            $set: method({
              node: props.nodeMap[link.target.id],
              colId: target_attr_id,
              value,
            }),
          },
        })
      }
      break
    }
    case SET_MANDATORY:
    case SET_FK_COL_OPTIONAL:
    case SET_REF_COL_OPTIONAL:
    case SET_REF_COL_MANDATORY: {
      method = diagramUtils.toggleNotNull
      if (type === SET_REF_COL_OPTIONAL || type === SET_REF_COL_MANDATORY) {
        nodeId = link.target.id
        node = props.nodeMap[nodeId]
        colId = target_attr_id
      }
      value = type === SET_MANDATORY || type === SET_REF_COL_MANDATORY
      break
    }
  }
  nodeMap = immutableUpdate(nodeMap, {
    [nodeId]: { $set: method({ node, colId, value }) },
  })
  updateAndDrawNodes({ nodeMap })
}

function assignCoord(nodeMap) {
  return props.nodes.reduce((map, n) => {
    if (!nodeMap[n.id]) map[n.id] = n
    else {
      const { x, y, vx, vy, size } = nodeMap[n.id]
      map[n.id] = {
        ...n,
        x,
        y,
        vx,
        vy,
        size,
      }
    }
    return map
  }, {})
}

/**
 * @param {array} v - diagram staging nodes with new coordinate values
 */
function onNodesCoordsUpdate(v) {
  const nodeMap = assignCoord(keyBy(v, 'id'))
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

function fitIntoView() {
  setZoom({ isFitIntoView: true })
}

function zoomIntoNode(node) {
  const minX = node.x - node.size.width / 2
  const minY = node.y - node.size.height / 2
  const maxX = minX + node.size.width
  const maxY = minY + node.size.height
  setZoom({
    isFitIntoView: true,
    customExtent: { minX, maxX, minY, maxY },
    /* add a padding of 20%, so there'd be some reserved space if the users
     * alter the table by adding new column
     */
    paddingPct: 20,
  })
}

/**
 * Auto adjust (zoom in or out) the contents of a graph
 * @param {Boolean} [param.isFitIntoView] - if it's true, v param will be ignored
 * @param {Object} [param.customExtent] - custom extent
 * @param {Number} [param.v] - zoom value
 */
function setZoom({ isFitIntoView: fitIntoView = false, customExtent, v, paddingPct = 2 }) {
  isFitIntoView.value = fitIntoView
  const extent = customExtent ? customExtent : entityDiagramRef.value.getExtent()
  panAndZoom.value = {
    ...getPanAndZoomValues({
      isFitIntoView: fitIntoView,
      extent,
      dim: diagramDim.value,
      scaleExtent: SCALE_EXTENT,
      paddingPct,
      customZoom: v,
    }),
    transition: true,
  }
}

/**
 * @public
 */
function updateNode(params) {
  entityDiagramRef.value.updateNode(params)
}

function createTbl() {
  if (connId.value) {
    const length = props.nodes.length
    const { genTblStructureData, genErdNode } = erdHelper
    const schema = typy(props.schemas, '[0]').safeString || 'test'
    const tableParser = new TableParser()
    const nodeData = genTblStructureData({
      parsedTable: tableParser.parse({
        ddl: ddlTemplate.createTbl(`table_${length + 1}`),
        schema,
        autoGenId: true,
      }),
      charsetCollationMap: charset_collation_map.value,
    })
    const { x, y, k } = panAndZoom.value
    const node = {
      ...genErdNode({ nodeData, highlightColor: dynamicColors(length) }),
      // plus extra padding
      x: (0 - x) / k + 65,
      y: (0 - y) / k + 42,
    }
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

function closeEditor() {
  ErdTaskTmp.update({
    where: props.erdTask.id,
    data: { active_entity_id: '', graph_height_pct: 100 },
  })
}

function updateAndDrawNodes({ nodeMap, skipHistory }) {
  ErdTask.update({ where: props.erdTask.id, data: { nodeMap } }).then(() => {
    entityDiagramRef.value.update(props.nodes)
    if (!skipHistory) erdTaskService.updateNodesHistory(nodeMap)
  })
}

function redrawnDiagram() {
  const nodeMap = nodesHistory.value[activeHistoryIdx.value]
  updateAndDrawNodes({ nodeMap, skipHistory: true })
}

async function navHistory(idx) {
  await erdTaskService.updateActiveHistoryIdx(idx)
  redrawnDiagram()
}

/**
 * Adds a PLAIN index for provided colId to provided node
 * @param {object} param
 * @param {string} param.colId
 * @param {object} param.node
 * @returns {object} updated node
 */
function addPlainIndex({ colId, node }) {
  const refTblDef = node.data.defs
  const plainKeyMap = typy(
    refTblDef,
    `key_category_map[${CREATE_TBL_TOKEN_MAP.key}]`
  ).safeObjectOrEmpty
  const newKey = erdHelper.genKey({ defs: refTblDef, category: CREATE_TBL_TOKEN_MAP.key, colId })
  return immutableUpdate(node, {
    data: {
      defs: {
        key_category_map: {
          $merge: { [CREATE_TBL_TOKEN_MAP.key]: { ...plainKeyMap, [newKey.id]: newKey } },
        },
      },
    },
  })
}

function createNewFk({ node, currentFkMap, newKey, refNode }) {
  let nodeMap = props.nodeMap

  // entity-diagram doesn't generate composite FK,so both cols and ref_cols always have one item
  const colId = newKey.cols[0].id
  const refColId = newKey.ref_cols[0].id
  // Compare column types
  if (
    erdHelper.validateFkColTypes({
      src: node,
      target: refNode,
      colId,
      targetColId: refColId,
    })
  ) {
    // Auto adds a PLAIN index for referenced col if there is none.
    const nonIndexedColId = colKeyCategoryMap.value[refColId] ? null : refColId
    if (nonIndexedColId) {
      nodeMap = immutableUpdate(nodeMap, {
        [refNode.id]: {
          $set: addPlainIndex({
            node: nodeMap[refNode.id],
            colId: nonIndexedColId,
          }),
        },
      })
    }

    // Add FK
    nodeMap = immutableUpdate(nodeMap, {
      [node.id]: {
        data: {
          defs: {
            key_category_map: {
              $merge: {
                [CREATE_TBL_TOKEN_MAP.foreignKey]: { ...currentFkMap, [newKey.id]: newKey },
              },
            },
          },
        },
      },
    })
    updateAndDrawNodes({ nodeMap })
  } else {
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
      text: [t('errors.fkColsRequirements')],
      type: SNACKBAR_TYPE_MAP.ERROR,
    })
  }
}

function rmFk(link) {
  let fkMap = typy(
    props.nodeMap[link.source.id],
    `data.defs.key_category_map[${CREATE_TBL_TOKEN_MAP.foreignKey}]`
  ).safeObjectOrEmpty

  fkMap = immutableUpdate(fkMap, { $unset: [link.id] })
  const nodeMap = immutableUpdate(props.nodeMap, {
    [link.source.id]: {
      data: {
        defs: {
          key_category_map: Object.keys(fkMap).length
            ? { $merge: { [CREATE_TBL_TOKEN_MAP.foreignKey]: fkMap } }
            : { $unset: [CREATE_TBL_TOKEN_MAP.foreignKey] },
        },
      },
    },
  })
  updateAndDrawNodes({ nodeMap })
}

function onClickAutoArrange() {
  ErdTask.update({
    where: props.erdTask.id,
    data: { is_laid_out: false },
  }).then(() => entityDiagramRef.value.runSimulation((diagram) => onRendered(diagram)))
}

function immutableUpdateConfig(obj, path, value) {
  const updatedObj = cloneDeep(obj)
  update(updatedObj, path, () => value)
  return updatedObj
}

function patchGraphConfig({ path, value }) {
  graphConfigData.value = immutableUpdateConfig(graphConfigData.value, path, value)
}

/**
 * @public
 * @returns {Promise<Canvas>}
 */
async function getCanvas() {
  return await html2canvas(entityDiagramRef.value.$el, { logging: false })
}

defineExpose({ updateNode, getCanvas })
</script>

<template>
  <div :id="DIAGRAM_ID" class="fill-height d-flex flex-column">
    <ErToolbar
      :graphConfig="graphConfigData"
      :height="TOOLBAR_HEIGHT"
      :zoom="panAndZoom.k"
      :isFitIntoView="isFitIntoView"
      :exportOptions="ERD_EXPORT_OPTS"
      :conn="conn"
      :nodesHistory="nodesHistory"
      :activeHistoryIdx="activeHistoryIdx"
      @set-zoom="setZoom"
      @on-create-table="createTbl"
      @on-undo="navHistory(activeHistoryIdx - 1)"
      @on-redo="navHistory(activeHistoryIdx + 1)"
      @click-auto-arrange="onClickAutoArrange"
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
      @on-create-new-fk="createNewFk($event)"
      @on-node-contextmenu="openCtxMenu({ type: NODE, e: $event.e, item: $event.node })"
      @on-link-contextmenu="openCtxMenu({ type: LINK, e: $event.e, item: $event.link })"
      @on-board-contextmenu="openCtxMenu({ type: BOARD, e: $event, activatorId: DIAGRAM_ID })"
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
      @create-tbl="createTbl($event)"
      @fit-into-view="fitIntoView($event)"
      @auto-arrange-erd="onClickAutoArrange($event)"
      @patch-graph-config="patchGraphConfig($event)"
      @open-editor="handleOpenEditor($event)"
      @rm-tbl="rmTbl($event)"
      @rm-fk="rmFk($event)"
      @update-cardinality="updateCardinality($event)"
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
