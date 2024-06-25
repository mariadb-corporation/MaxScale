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
import ErdTask from '@wsModels/ErdTask'
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import ErToolbar from '@wkeComps/ErdWke/ErToolbar.vue'
import EntityDiagram from '@wkeComps/ErdWke/EntityDiagram.vue'
import erdTaskService from '@wsServices/erdTaskService'
import { LINK_SHAPES } from '@/components/svgGraph/shapeConfig'
import { EVENT_TYPES } from '@/components/svgGraph/linkConfig'
import { MIN_MAX_CARDINALITY } from '@wkeComps/ErdWke/config'
import tableTemplate from '@wkeComps/ErdWke/tableTemplate'
import erdHelper from '@/utils/erdHelper'
import TableParser from '@/utils/TableParser'
import { DIAGRAM_CTX_TYPES } from '@/constants'
import {
  DDL_EDITOR_SPECS,
  CREATE_TBL_TOKENS,
  ENTITY_OPT_TYPES,
  LINK_OPT_TYPES,
} from '@/constants/workspace'
import html2canvas from 'html2canvas'

const props = defineProps({
  isFormValid: { type: Boolean, required: true },
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
  lodash: { merge, isEqual, keyBy, cloneDeep, update },
  immutableUpdate,
  dynamicColors,
  uuidv1,
  getPanAndZoomValues,
} = useHelpers()

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
const ctxMenuType = ref(null) // DIAGRAM_CTX_TYPES
const activeCtxItem = ref(null)
const showCtxMenu = ref(false)
const menuX = ref(0)
const menuY = ref(0)

const charset_collation_map = computed(() => store.state.ddlEditor.charset_collation_map)

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
const boardOpts = computed(() => [
  { title: t('createTable'), action: () => handleCreateTable() },
  { title: t('fitDiagramInView'), action: () => fitIntoView() },
  { title: t('autoArrangeErd'), action: () => onClickAutoArrange() },
  {
    title: t(
      graphConfigData.value.link.isAttrToAttr ? 'disableDrawingFksToCols' : 'enableDrawingFksToCols'
    ),
    action: () =>
      changeGraphConfigAttrValue({
        path: 'link.isAttrToAttr',
        value: !graphConfigData.value.link.isAttrToAttr,
      }),
  },
  {
    title: t(
      graphConfigData.value.link.isHighlightAll
        ? 'turnOffRelationshipHighlight'
        : 'turnOnRelationshipHighlight'
    ),
    action: () =>
      changeGraphConfigAttrValue({
        path: 'link.isHighlightAll',
        value: !graphConfigData.value.link.isHighlightAll,
      }),
  },
  { title: t('export'), children: ERD_EXPORT_OPTS },
])
const entityOpts = computed(() =>
  Object.values(ENTITY_OPT_TYPES).map((type) => ({
    type,
    title: t(type),
    action: () => handleChooseNodeOpt({ type, node: activeCtxItem.value }),
  }))
)
const linkOpts = computed(() => {
  const { EDIT, REMOVE } = LINK_OPT_TYPES
  const link = activeCtxItem.value
  let opts = [
    { title: t(EDIT), type: EDIT },
    { title: t(REMOVE), type: REMOVE },
  ]
  if (link) {
    opts.push(genCardinalityOpt(link))
    const {
      relationshipData: { src_attr_id, target_attr_id },
    } = link
    const colKeyCategories = colKeyCategoryMap.value[src_attr_id]
    const refColKeyCategories = colKeyCategoryMap.value[target_attr_id]
    if (!colKeyCategories.includes(CREATE_TBL_TOKENS.primaryKey))
      opts.push(genOptionalityOpt({ link }))
    if (!refColKeyCategories.includes(CREATE_TBL_TOKENS.primaryKey))
      opts.push(genOptionalityOpt({ link, isForRefTbl: true }))
  }
  return opts.map((opt) => ({ ...opt, action: () => handleChooseLinkOpt(opt.type) }))
})
const ctxMenuItems = computed(() => {
  switch (ctxMenuType.value) {
    case DIAGRAM_CTX_TYPES.BOARD:
      return boardOpts.value
    case DIAGRAM_CTX_TYPES.NODE:
      return entityOpts.value
    case DIAGRAM_CTX_TYPES.LINK:
      return linkOpts.value
    default:
      return []
  }
})
const activeCtxItemId = computed(() => typy(activeCtxItem.value, 'id').safeString)
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
watch(showCtxMenu, (v) => {
  if (!v) activeCtxItem.value = null
})

onBeforeMount(() => (graphConfigData.value = merge(graphConfigData.value, activeGraphConfig.value)))

function onRendered(diagram) {
  onNodesCoordsUpdate(diagram.nodes)
  if (diagram.nodes.length) fitIntoView()
}

function handleDblClickNode(node) {
  handleChooseNodeOpt({ type: ENTITY_OPT_TYPES.EDIT, node })
}

function genCardinalityOpt(link) {
  const { SET_ONE_TO_ONE, SET_ONE_TO_MANY } = LINK_OPT_TYPES
  const { ONLY_ONE, ZERO_OR_ONE } = MIN_MAX_CARDINALITY
  const [src = ''] = link.relationshipData.type.split(':')
  const optType = src === ONLY_ONE || src === ZERO_OR_ONE ? SET_ONE_TO_MANY : SET_ONE_TO_ONE
  return { title: t(optType), type: optType }
}

function genOptionalityOpt({ link, isForRefTbl = false }) {
  const { SET_MANDATORY, SET_FK_COL_OPTIONAL, SET_REF_COL_MANDATORY, SET_REF_COL_OPTIONAL } =
    LINK_OPT_TYPES
  const {
    source,
    target,
    relationshipData: { src_attr_id, target_attr_id },
  } = link
  let node = source,
    colId = src_attr_id,
    optType = isForRefTbl ? SET_REF_COL_MANDATORY : SET_MANDATORY

  if (isForRefTbl) {
    node = target
    colId = target_attr_id
  }
  if (erdHelper.isColMandatory({ node, colId }))
    optType = isForRefTbl ? SET_REF_COL_OPTIONAL : SET_FK_COL_OPTIONAL

  return { title: t(optType), type: optType }
}

function handleOpenCtxMenu({ e, type, item }) {
  menuX.value = e.clientX
  menuY.value = e.clientY
  ctxMenuType.value = type
  activeCtxItem.value = item
  showCtxMenu.value = true
}

function handleChooseNodeOpt({ type, node, skipZoom = false }) {
  const { EDIT, REMOVE } = ENTITY_OPT_TYPES
  switch (type) {
    case EDIT: {
      handleOpenEditor({ node, spec: DDL_EDITOR_SPECS.COLUMNS })
      if (connId.value && !skipZoom)
        // call in the next tick to ensure diagramDim height is up to date
        nextTick(() => zoomIntoNode(node))

      break
    }
    case REMOVE: {
      const nodeMap = props.nodes.reduce((map, n) => {
        if (n.id !== node.id) {
          const fkMap = n.data.defs.key_category_map[CREATE_TBL_TOKENS.foreignKey]
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
                    ? { $merge: { [CREATE_TBL_TOKENS.foreignKey]: updatedFkMap } }
                    : { $unset: [CREATE_TBL_TOKENS.foreignKey] },
                },
              },
            })
          }
        }

        return map
      }, {})
      closeEditor()
      updateAndDrawNodes({ nodeMap })
      break
    }
  }
}

function handleChooseLinkOpt(type) {
  const link = activeCtxItem.value
  const {
    EDIT,
    REMOVE,
    SET_ONE_TO_ONE,
    SET_ONE_TO_MANY,
    SET_MANDATORY,
    SET_FK_COL_OPTIONAL,
    SET_REF_COL_MANDATORY,
    SET_REF_COL_OPTIONAL,
  } = LINK_OPT_TYPES
  switch (type) {
    case EDIT:
      handleOpenEditor({ node: link.source, spec: DDL_EDITOR_SPECS.FK })
      if (connId.value) nextTick(() => zoomIntoNode(link.source))
      break
    case REMOVE: {
      let fkMap = typy(
        props.nodeMap[link.source.id],
        `data.defs.key_category_map[${CREATE_TBL_TOKENS.foreignKey}]`
      ).safeObjectOrEmpty
      fkMap = immutableUpdate(fkMap, { $unset: [link.id] })
      const nodeMap = immutableUpdate(props.nodeMap, {
        [link.source.id]: {
          data: {
            defs: {
              key_category_map: Object.keys(fkMap).length
                ? { $merge: { [CREATE_TBL_TOKENS.foreignKey]: fkMap } }
                : { $unset: [CREATE_TBL_TOKENS.foreignKey] },
            },
          },
        },
      })
      updateAndDrawNodes({ nodeMap })
      break
    }
    case SET_ONE_TO_MANY:
    case SET_ONE_TO_ONE:
    case SET_FK_COL_OPTIONAL:
    case SET_MANDATORY:
    case SET_REF_COL_OPTIONAL:
    case SET_REF_COL_MANDATORY:
      updateCardinality({ type, link })
      break
  }
}

function updateCardinality({ type, link }) {
  const {
    SET_ONE_TO_ONE,
    SET_ONE_TO_MANY,
    SET_MANDATORY,
    SET_FK_COL_OPTIONAL,
    SET_REF_COL_MANDATORY,
    SET_REF_COL_OPTIONAL,
  } = LINK_OPT_TYPES
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
      method = toggleUnique
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
      method = toggleNotNull
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

/**
 * @param {object} param
 * @param {object} param.node - entity-diagram node
 * @param {string} param.colId - column id
 * @param {boolean} param.value - if it's true, add UQ key if not exists, otherwise remove UQ
 * @return {object} updated node
 */
function toggleUnique({ node, colId, value }) {
  const category = CREATE_TBL_TOKENS.uniqueKey
  // check if column is already unique
  const isUnique = erdHelper.areUniqueCols({ node, colIds: [colId] })
  if (value && isUnique) return node
  let keyMap = node.data.defs.key_category_map[category] || {}
  // add UQ key
  if (value) {
    const newKey = erdHelper.genKey({
      defs: node.data.defs,
      category,
      colId,
    })
    keyMap = immutableUpdate(keyMap, { $merge: { [newKey.id]: newKey } })
  }
  // remove UQ key
  else
    keyMap = immutableUpdate(keyMap, {
      $unset: Object.values(keyMap).reduce((ids, k) => {
        if (
          isEqual(
            k.cols.map((c) => c.id),
            [colId]
          )
        )
          ids.push(k.id)
        return ids
      }, []),
    })

  return immutableUpdate(node, {
    data: {
      defs: {
        key_category_map: Object.keys(keyMap).length
          ? { $merge: { [category]: keyMap } }
          : { $unset: [category] },
      },
    },
  })
}

/**
 * @param {object} param
 * @param {object} param.node - entity-diagram node
 * @param {string} param.colId - column id
 * @param {boolean} param.value - if it's true, turns on NOT NULL.
 * @return {object} updated node
 */
function toggleNotNull({ node, colId, value }) {
  return immutableUpdate(node, {
    data: { defs: { col_map: { [colId]: { nn: { $set: value } } } } },
  })
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

function updateNode(params) {
  entityDiagramRef.value.updateNode(params)
}

function handleCreateTable() {
  if (connId.value) {
    const length = props.nodes.length
    const { genDdlEditorData, genErdNode } = erdHelper
    const schema = typy(props.schemas, '[0]').safeString || 'test'
    const tableParser = new TableParser()
    const nodeData = genDdlEditorData({
      parsedTable: tableParser.parse({
        ddl: tableTemplate(`table_${length + 1}`),
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
      handleChooseNodeOpt({
        type: ENTITY_OPT_TYPES.EDIT,
        node,
        skipZoom: true,
      })
    })
  } else
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
      text: [t('errors.requiredConn')],
      type: 'error',
    })
}

function handleOpenEditor({ node, spec }) {
  if (connId.value) {
    let data = { active_entity_id: node.id, active_spec: spec }
    if (props.graphHeightPct === 100) data.graph_height_pct = 40
    ErdTaskTmp.update({ where: props.erdTask.id, data })
  } else
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
      text: [t('errors.requiredConn')],
      type: 'error',
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
    `key_category_map[${CREATE_TBL_TOKENS.key}]`
  ).safeObjectOrEmpty
  const newKey = erdHelper.genKey({ defs: refTblDef, category: CREATE_TBL_TOKENS.key, colId })
  return immutableUpdate(node, {
    data: {
      defs: {
        key_category_map: {
          $merge: { [CREATE_TBL_TOKENS.key]: { ...plainKeyMap, [newKey.id]: newKey } },
        },
      },
    },
  })
}

function onCreateNewFk({ node, currentFkMap, newKey, refNode }) {
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
                [CREATE_TBL_TOKENS.foreignKey]: { ...currentFkMap, [newKey.id]: newKey },
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
      type: 'error',
    })
  }
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

function changeGraphConfigAttrValue({ path, value }) {
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
      @on-create-table="handleCreateTable"
      @on-undo="navHistory(activeHistoryIdx - 1)"
      @on-redo="navHistory(activeHistoryIdx + 1)"
      @click-auto-arrange="onClickAutoArrange"
      @change-graph-config-attr-value="changeGraphConfigAttrValue"
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
      @on-rendered.once="onRendered"
      @on-node-drag-end="onNodeDragEnd"
      @dblclick="isFormValid ? handleDblClickNode($event) : null"
      @on-create-new-fk="onCreateNewFk"
      @on-node-contextmenu="
        handleOpenCtxMenu({ type: DIAGRAM_CTX_TYPES.NODE, e: $event.e, item: $event.node })
      "
      @on-link-contextmenu="
        handleOpenCtxMenu({ type: DIAGRAM_CTX_TYPES.LINK, e: $event.e, item: $event.link })
      "
      @on-board-contextmenu="
        handleOpenCtxMenu({ type: DIAGRAM_CTX_TYPES.BOARD, e: $event, item: { id: DIAGRAM_ID } })
      "
    >
      <template #entity-setting-btn="{ node, isHovering }">
        <VBtn
          :id="node.id"
          size="small"
          class="setting-btn"
          :class="{ 'setting-btn--visible': isHovering || activeCtxItemId === node.id }"
          icon
          variant="text"
          density="compact"
          color="primary"
          :disabled="!isFormValid"
          @click.stop="handleOpenCtxMenu({ e: $event, type: DIAGRAM_CTX_TYPES.NODE, item: node })"
        >
          <VIcon size="14" icon="mxs:settings" />
        </VBtn>
      </template>
    </EntityDiagram>
    <CtxMenu
      v-if="activeCtxItemId"
      :key="activeCtxItemId"
      v-model="showCtxMenu"
      :items="ctxMenuItems"
      :target="[menuX, menuY]"
      transition="slide-y-transition"
      content-class="full-border"
      :activator="`#${activeCtxItemId}`"
      @item-click="$event.action()"
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
