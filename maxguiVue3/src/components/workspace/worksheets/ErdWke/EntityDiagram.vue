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
import { forceSimulation, forceLink, forceCenter, forceCollide, forceX, forceY } from 'd3-force'
import { min as d3Min, max as d3Max } from 'd3-array'
import GraphConfig from '@/components/svgGraph/GraphConfig'
import EntityLink from '@wkeComps/ErdWke/EntityLink'
import { EVENT_TYPES } from '@/components/svgGraph/linkConfig'
import { LINK_SHAPES } from '@/components/svgGraph/shapeConfig'
import { getConfig } from '@wkeComps/ErdWke/config'
import erdHelper from '@/utils/erdHelper'
import { CREATE_TBL_TOKENS, REF_OPTS } from '@/constants/workspace'
import ErdKeyIcon from '@wkeComps/ErdWke/ErdKeyIcon.vue'
import RefPoints from '@wkeComps/ErdWke/RefPoints.vue'

defineOptions({ inheritAttrs: false })
const props = defineProps({
  dim: { type: Object, required: true },
  panAndZoom: { type: Object, required: true },
  scaleExtent: { type: Array, required: true },
  nodes: { type: Array, required: true },
  graphConfigData: { type: Object, required: true },
  isLaidOut: { type: Boolean, default: false },
  activeNodeId: { type: String, default: '' },
  refTargetMap: { type: Object, required: true },
  tablesColNameMap: { type: Object, required: true },
  colKeyCategoryMap: { type: Object, required: true },
})
const emit = defineEmits([
  'update:panAndZoom',
  'on-rendered', // ({ nodes:array, links:array })
  'on-link-contextmenu', // ({e: Event, link:object})
  'on-node-contextmenu', // ({e: Event, node:object})
  'on-node-drag-end', // (node)
  'on-create-new-fk', // ({ node:object, currentFkMap: object, newKey: object, refNode: object, })
  'dblclick',
])

const {
  lodash: { cloneDeep, keyBy, merge },
  delay,
  deepDiff,
  uuidv1,
  doubleRAF,
} = useHelpers()
const typy = useTypy()
const tdMaxWidth = 320 / 2 - 27 // entity max-width / 2 - offset. Offset includes padding and border

const graphNodesRef = ref(null)
const isRendering = ref(false)
const linkContainer = ref(null)
const graphNodeCoordMap = ref({})
const graphNodes = ref([])
const graphLinks = ref([])
const simulation = ref(null)
const defNodeSize = ref({ width: 250, height: 100 })
const chosenLinks = ref([])
const entityLink = ref(null)
const graphConfig = ref(null)
const isDraggingNode = ref(false)
const graphDim = ref({})
const clickedNodeId = ref('')
const clickOutside = ref(true)
const refTarget = ref(null)
const isDrawingFk = ref(false)
const hoveredLink = ref(null)
const tooltipX = ref(0)
const tooltipY = ref(0)

const panAndZoomData = computed({
  get: () => props.panAndZoom,
  set: (v) => emit('update:panAndZoom', v),
})
const entityKeyCategoryMap = computed(() =>
  graphNodes.value.reduce((map, node) => {
    map[node.id] = node.data.defs.key_category_map
    return map
  }, {})
)
const nodeMap = computed(() => keyBy(graphNodes.value, 'id'))
const entitySizeConfig = computed(() => props.graphConfigData.linkShape.entitySizeConfig)
const highlightColStyleMap = computed(() =>
  chosenLinks.value.reduce((map, link) => {
    const {
      source,
      target,
      relationshipData: { src_attr_id, target_attr_id },
      styles: { invisibleHighlightColor },
    } = link

    if (!map[source.id]) map[source.id] = []
    if (!map[target.id]) map[target.id] = []
    const style = {
      backgroundColor: invisibleHighlightColor,
      color: 'white',
    }
    map[source.id].push({ col: src_attr_id, ...style })
    map[target.id].push({ col: target_attr_id, ...style })
    return map
  }, {})
)
const isAttrToAttr = computed(() => typy(props.graphConfigData, 'link.isAttrToAttr').safeBoolean)
const isHighlightAll = computed(
  () => typy(props.graphConfigData, 'link.isHighlightAll').safeBoolean
)
const isStraightShape = computed(
  () => typy(props.graphConfigData, 'linkShape.type').safeString === LINK_SHAPES.STRAIGHT
)
const globalLinkColor = computed(() => typy(props.graphConfigData, 'link.color').safeString)
const hoverable = computed(() => Boolean(!clickedNodeId.value))
const hoveredFk = computed(() =>
  hoveredLink.value
    ? getFks(hoveredLink.value.source).find((key) => key.id === hoveredLink.value.id)
    : null
)
const hoveredFkId = computed(() => typy(hoveredFk.value, 'id').safeString)
const hoveredFkInfo = computed(() => {
  if (
    hoveredFk.value &&
    !typy(props.refTargetMap).isEmptyObject &&
    !typy(props.tablesColNameMap).isEmptyObject
  ) {
    return erdHelper.genConstraint({
      key: hoveredFk.value,
      refTargetMap: props.refTargetMap,
      tablesColNameMap: props.tablesColNameMap,
      stagingColNameMap: typy(props.tablesColNameMap, `[${hoveredLink.value.source.id}]`),
    })
  }
  return ''
})
watch(hoverable, (v) => {
  if (!v) mouseleaveNode()
})

onBeforeMount(() => initGraphConfig())

onMounted(() => {
  if (props.nodes.length) {
    isRendering.value = true
    assignData(props.nodes)
  } else emitOnRendered()
  graphDim.value = props.dim
})

let unwatch_graphConfigData
onBeforeUnmount(() => typy(unwatch_graphConfigData).safeFunction())

/**
 * D3 mutates nodes which breaks reactivity, so to prevent that,
 * nodes must be cloned.
 * By assigning `nodes` to `graphNodes`,  @node-size-map event will be
 * emitted from `SvgGraphNodes` component which triggers the drawing
 * of the graph if there is a change in the ID of the nodes.
 * @param {Array} nodes - erd nodes
 */
function assignData(nodes) {
  const allNodes = cloneDeep(nodes)
  graphNodes.value = allNodes
  genLinks(allNodes)
}

function genLinks(nodes) {
  graphLinks.value = nodes.reduce((links, node) => {
    const fks = getFks(node)
    fks.forEach((fk) => {
      links = [
        ...links,
        ...erdHelper.handleGenErdLink({
          srcNode: node,
          fk,
          nodes,
          isAttrToAttr: isAttrToAttr.value,
          colKeyCategoryMap: props.colKeyCategoryMap,
        }),
      ]
    })
    return links
  }, [])
}

function getNodeIdx(id) {
  return graphNodes.value.findIndex((n) => n.id === id)
}

/**
 * @public
 * Call this method to update the data of a node
 */
function updateNode({ id, data }) {
  const index = getNodeIdx(id)
  if (index >= 0) {
    graphNodes.value[index].data = data
    // Re-calculate the size
    graphNodesRef.value.onNodeResized(id)
    genLinks(graphNodes.value)
  }
}

/**
 * @public
 * Call this method to add a new node
 */
function addNode(node) {
  graphNodes.value.push(node)
  genLinks(graphNodes.value)
}

/**
 * @public
 * Get the correct dimension of the nodes for controlling the zoom
 */
function getGraphExtent() {
  return {
    minX: d3Min(graphNodes.value, (n) => n.x - n.size.width / 2) || 0,
    minY: d3Min(graphNodes.value, (n) => n.y - n.size.height / 2) || 0,
    maxX: d3Max(graphNodes.value, (n) => n.x + n.size.width / 2) || graphDim.value.width,
    maxY: d3Max(graphNodes.value, (n) => n.y + n.size.height / 2) || graphDim.value.height,
  }
}

/**
 * @public
 * @param {Array} nodes - erd nodes
 */
function update(nodes) {
  assignData(nodes)
  // setNodeSizeMap will trigger updateNodeSizes method
  nextTick(() => graphNodesRef.value.setNodeSizeMap())
}

/**
 * @param {Object} nodeSizeMap - size of nodes
 */
function updateNodeSizes(nodeSizeMap) {
  graphNodes.value.forEach((node) => (node.size = nodeSizeMap[node.id]))
  if (Object.keys(nodeSizeMap).length) runSimulation()
}

/**
 * @public
 * @param {function} cb - callback function to be run after the simulation is end
 */
function runSimulation(cb) {
  let tickCount = 1
  simulation.value = forceSimulation(graphNodes.value)
    .force(
      'link',
      forceLink(graphLinks.value)
        .id((d) => d.id)
        .strength(0.5)
        .distance(100)
    )
    .force('center', forceCenter(props.dim.width / 2, props.dim.height / 2))
    .force('x', forceX().strength(0.1))
    .force('y', forceY().strength(0.1))
  if (props.isLaidOut) {
    simulation.value.stop()
    // Adding a loading animation can enhance the smoothness, even if the graph is already laid out.
    delay(isRendering.value ? 300 : 0).then(() => {
      draw(tickCount)
      emitOnRendered()
    })
  } else {
    /**
     * TODO: Make alphaMin customizable by the user, the smaller the number is, the better
     * the layout would be but it also takes longer time.
     */
    simulation.value
      .alphaMin(0.1)
      .on('tick', () => {
        draw(tickCount)
        tickCount++
      })
      .on('end', () => {
        emitOnRendered()
        typy(cb).safeFunction({
          nodes: graphNodes.value,
          links: graphLinks.value,
        })
      })
    handleCollision()
  }
}

function handleCollision() {
  simulation.value.force(
    'collide',
    forceCollide().radius((d) => {
      const { width, height } = d.size
      // Because nodes are densely packed,  this adds an extra radius of 50 pixels to the nodes
      return Math.sqrt(width * width + height * height) / 2 + 50
    })
  )
}

function initGraphConfig() {
  graphConfig.value = new GraphConfig(merge(getConfig(), props.graphConfigData))
}

function initLinkInstance() {
  entityLink.value = new EntityLink(graphConfig.value.config)
  watchConfig()
}

function emitOnRendered() {
  // after finish rendering, set styles for all links
  if (isHighlightAll.value) handleHighlightAllLinks()
  emit('on-rendered', { nodes: graphNodes.value, links: graphLinks.value })
}

/**
 * @param {number} tickCount - number of times this function is called.
 * This helps to prevent initLinkInstance from being called repeatedly memory which
 * causes memory leaks. initLinkInstance should be called once
 */
function draw(tickCount) {
  setGraphNodeCoordMap()
  if (tickCount === 1) initLinkInstance()
  drawLinks()
  if (tickCount === 1) isRendering.value = false
}

function setGraphNodeCoordMap() {
  graphNodeCoordMap.value = graphNodes.value.reduce((map, n) => {
    const { x, y, id } = n
    if (id) map[id] = { x, y }
    return map
  }, {})
}

function getLinks() {
  return simulation.value.force('link').links()
}
function setEventStyles({ links, eventType }) {
  entityLink.value.setEventStyles({
    links,
    eventType,
    evtStylesMod: () => (isStraightShape.value ? { color: globalLinkColor.value } : null),
  })
}

function handleMouseOverOut({ e, link, linkCtr, pathGenerator, eventType }) {
  hoveredLink.value = link
  tooltipX.value = e.clientX
  tooltipY.value = e.clientY
  if (!isHighlightAll.value) {
    if (eventType === EVENT_TYPES.HOVER) mouseenterNode({ node: link.source })
    else mouseleaveNode()
    setEventStyles({ links: [link], eventType })
    entityLink.value.drawPaths({ linkCtr, joinType: 'update', pathGenerator })
    entityLink.value.drawMarkers({ linkCtr, joinType: 'update' })
  }
}

function openContextMenu(param) {
  const { e, link } = param
  e.preventDefault()
  e.stopPropagation()
  emit('on-link-contextmenu', { e, link })
}

function drawLinks() {
  entityLink.value.draw({
    containerEle: linkContainer.value,
    data: getLinks().filter((link) => !link.hidden),
    events: {
      mouseover: (param) =>
        handleMouseOverOut({
          ...param,
          eventType: EVENT_TYPES.HOVER,
        }),
      mouseout: (param) =>
        handleMouseOverOut({
          ...param,
          eventType: EVENT_TYPES.NONE,
        }),
      contextmenu: (param) => openContextMenu(param),
      click: (param) => openContextMenu(param),
    },
  })
}

function setChosenLinks(node) {
  chosenLinks.value = erdHelper.getNodeLinks({ links: getLinks(), node })
}

function setEventLinkStyles(eventType) {
  setEventStyles({ eventType, links: chosenLinks.value })
  drawLinks()
}

function onNodeDrag({ node, diffX, diffY }) {
  const nodeData = nodeMap.value[node.id]
  nodeData.x = nodeData.x + diffX
  nodeData.y = nodeData.y + diffY
  if (!isHighlightAll.value) {
    setChosenLinks(node)
    if (!isDraggingNode.value) setEventLinkStyles(EVENT_TYPES.DRAGGING)
  }
  isDraggingNode.value = true
  /**
   * drawLinks is called inside setEventLinkStyles method but it run once.
   * To ensure that the paths of links continue to be redrawn, call it again while
   * dragging the node
   */
  drawLinks()
}

function onNodeDragEnd({ node }) {
  if (isDraggingNode.value) {
    if (!isHighlightAll.value) {
      setEventLinkStyles(EVENT_TYPES.NONE)
      chosenLinks.value = []
    }
    isDraggingNode.value = false
    emit('on-node-drag-end', node)
  }
}

function mouseenterNode({ node }) {
  if (!isHighlightAll.value) {
    setChosenLinks(node)
    setEventLinkStyles(EVENT_TYPES.HOVER)
  }
}

function mouseleaveNode() {
  if (!isHighlightAll.value) {
    setEventLinkStyles(EVENT_TYPES.NONE)
    chosenLinks.value = []
  }
}

function getKeyIcon({ node, colId }) {
  const { primaryKey, uniqueKey, key, fullTextKey, spatialKey, foreignKey } = CREATE_TBL_TOKENS

  const { color } = getHighlightColStyle({ node, colId }) || {}
  const categories = props.colKeyCategoryMap[colId] || []

  let isUQ = false
  if (categories.includes(uniqueKey)) {
    isUQ = erdHelper.isSingleUQ({
      keyCategoryMap: entityKeyCategoryMap.value[node.id],
      colId,
    })
  }

  if (categories.includes(primaryKey))
    return {
      icon: '$mdiKey',
      color: color ? color : 'primary',
      size: 18,
    }
  else if (isUQ)
    return {
      icon: 'mxs:uniqueIndexKey',
      color: color ? color : 'navigation',
      size: 16,
    }
  else if ([key, fullTextKey, spatialKey, foreignKey].some((k) => categories.includes(k)))
    return {
      icon: 'mxs:indexKey',
      color: color ? color : 'navigation',
      size: 16,
    }
}

function getHighlightColStyle({ node, colId }) {
  const cols = highlightColStyleMap.value[node.id] || []
  return cols.find((item) => item.col === colId)
}

function watchConfig() {
  unwatch_graphConfigData = watch(
    () => props.graphConfigData,
    (v, oV) => {
      /**
       * Because only one attribute can be changed at a time, so it's safe to
       * access the diff with hard-code indexes.
       */
      const diff = typy(deepDiff(oV, v), '[0]').safeObjectOrEmpty
      const path = diff.path.join('.')
      const value = diff.rhs
      graphConfig.value.updateConfig({ path, value })
      switch (path) {
        case 'link.isAttrToAttr':
          handleIsAttrToAttrMode(value)
          break
        case 'link.isHighlightAll':
          handleHighlightAllLinks()
          break
      }
      drawLinks()
    },
    { deep: true }
  )
}

function handleHighlightAllLinks() {
  if (isHighlightAll.value) chosenLinks.value = graphLinks.value
  setEventLinkStyles(isHighlightAll.value ? EVENT_TYPES.HOVER : EVENT_TYPES.NONE)
  if (!isHighlightAll.value) chosenLinks.value = []
}

/**
 * If value is true, the diagram shows all links including composite links for composite keys
 * @param {boolean} v
 */
function handleIsAttrToAttrMode(v) {
  graphLinks.value.forEach((link) => {
    if (link.isPartOfCompositeKey) link.hidden = !v
  })
  simulation.value.force('link').links(graphLinks.value)
}

function onDrawingFk() {
  clickOutside.value = false
  isDrawingFk.value = true
}

function getFkMap(node) {
  return typy(entityKeyCategoryMap.value, `[${node.id}][${CREATE_TBL_TOKENS.foreignKey}]`)
    .safeObjectOrEmpty
}

function getFks(node) {
  return Object.values(getFkMap(node))
}

function onEndDrawFk({ node, cols }) {
  isDrawingFk.value = false
  if (refTarget.value) {
    const currentFkMap = getFkMap(node)
    emit('on-create-new-fk', {
      node,
      currentFkMap,
      newKey: {
        id: `key_${uuidv1()}`,
        name: `${node.data.options.name}_ibfk_${Object.keys(currentFkMap).length}`,
        cols,
        ...refTarget.value.data,
      },
      refNode: refTarget.value.node,
    })
    refTarget.value = null
    clickOutside.value = true // hide ref-points
  } else doubleRAF(() => (clickOutside.value = true))
}

function setRefTargetData({ node, col }) {
  refTarget.value = {
    data: {
      ref_cols: [{ id: col.id }],
      ref_tbl_id: node.id,
      on_delete: REF_OPTS.NO_ACTION,
      on_update: REF_OPTS.NO_ACTION,
    },
    node,
  }
}

defineExpose({ runSimulation, updateNode, addNode, getGraphExtent, update })
</script>

<template>
  <div class="fill-height er-diagram">
    <div v-if="isDraggingNode" class="dragging-mask" />
    <VProgressLinear v-if="isRendering" indeterminate color="primary" />
    <SvgGraphBoard
      v-model="panAndZoomData"
      :scaleExtent="scaleExtent"
      :style="{ visibility: isRendering ? 'hidden' : 'visible' }"
      :dim="dim"
      :graphDim="graphDim"
      @get-graph-ctr="linkContainer = $event"
      v-bind="$attrs"
    >
      <template #append="{ data: { style } }">
        <SvgGraphNodes
          ref="graphNodesRef"
          v-model:coordMap="graphNodeCoordMap"
          v-model:clickedNodeId="clickedNodeId"
          :nodes="graphNodes"
          :style="style"
          :nodeStyle="{ userSelect: 'none' }"
          :defNodeSize="defNodeSize"
          draggable
          :hoverable="hoverable"
          :boardZoom="panAndZoomData.k"
          autoWidth
          dblclick
          contextmenu
          click
          :clickOutside="clickOutside"
          @node-size-map="updateNodeSizes"
          @drag="onNodeDrag"
          @drag-end="onNodeDragEnd"
          @mouseenter="mouseenterNode"
          @mouseleave="mouseleaveNode"
          @on-node-contextmenu="emit('on-node-contextmenu', $event)"
          @dblclick="emit('dblclick', $event)"
        >
          <template #default="{ data: { node } }">
            <div
              v-if="node.id === activeNodeId"
              class="active-node-border-div absolute rounded-lg"
            />
            <RefPoints
              v-if="node.id === clickedNodeId"
              :node="node"
              :entitySizeConfig="entitySizeConfig"
              :linkContainer="linkContainer"
              :boardZoom="panAndZoomData.k"
              :graphConfig="graphConfig.config"
              @drawing="onDrawingFk"
              @draw-end="onEndDrawFk"
            />
            <VHover>
              <template #default="{ isHovering, props }">
                <table
                  class="entity-table"
                  :style="{ borderColor: node.styles.highlightColor }"
                  v-bind="props"
                >
                  <thead>
                    <tr :style="{ height: `${entitySizeConfig.headerHeight}px` }">
                      <th
                        class="text-center font-weight-bold text-no-wrap rounded-t-lg pl-4 pr-1"
                        colspan="3"
                      >
                        <div class="d-flex flex-row align-center justify-center">
                          <div class="flex-grow-1">
                            {{ node.data.options.name }}
                          </div>
                          <slot name="entity-setting-btn" :node="node" :isHovering="isHovering" />
                        </div>
                      </th>
                    </tr>
                  </thead>
                  <tbody>
                    <tr
                      v-for="(col, colId) in node.data.defs.col_map"
                      :key="colId"
                      :style="{
                        height: `${entitySizeConfig.rowHeight}px`,
                        ...getHighlightColStyle({ node, colId }),
                      }"
                      v-on="
                        isDrawingFk
                          ? {
                              mouseenter: () => setRefTargetData({ node, col }),
                              mouseleave: () => (refTarget = null),
                            }
                          : {}
                      "
                    >
                      <td>
                        <ErdKeyIcon
                          class="fill-height d-flex align-center"
                          :data="getKeyIcon({ node, colId })"
                        />
                      </td>
                      <td>
                        <GblTooltipActivator
                          :data="{ txt: col.name }"
                          :debounce="0"
                          fillHeight
                          :maxWidth="tdMaxWidth"
                          activateOnTruncation
                        />
                      </td>
                      <td
                        class="text-end"
                        :style="{
                          color:
                            $typy(getHighlightColStyle({ node, colId }), 'color').safeString ||
                            '#6c7c7b',
                        }"
                      >
                        <GblTooltipActivator
                          :data="{ txt: col.data_type }"
                          :debounce="0"
                          fillHeight
                          :maxWidth="tdMaxWidth"
                          activateOnTruncation
                        />
                      </td>
                    </tr>
                  </tbody>
                </table>
              </template>
            </VHover>
          </template>
        </SvgGraphNodes>
      </template>
    </SvgGraphBoard>
    <VTooltip
      v-if="hoveredFkId"
      :key="hoveredFkId"
      location="bottom"
      transition="slide-y-transition"
      :activator="`#${hoveredFkId}`"
      :target="[tooltipX, tooltipY]"
      :open-delay="300"
    >
      <pre>{{ hoveredFkInfo }}</pre>
    </VTooltip>
  </div>
</template>

<style lang="scss" scoped>
.entity-table {
  background: white;
  width: 100%;
  border-spacing: 0px;
  tr,
  thead,
  tbody {
    border-color: inherit;
  }
  thead {
    th {
      border-top: 7px solid;
      border-right: 1px solid;
      border-bottom: 1px solid;
      border-left: 1px solid;
      border-color: inherit;
    }
  }
  tbody {
    tr {
      &:hover {
        background: colors.$tr-hovered-color;
      }
      td {
        white-space: nowrap;
        padding: 0px 8px;
        &:first-of-type {
          padding-left: 8px;
          padding-right: 0px;
          border-left: 1px solid;
          border-color: inherit;
        }
        &:nth-of-type(2) {
          padding-left: 2px;
        }
        &:last-of-type {
          border-right: 1px solid;
          border-color: inherit;
        }
      }
      &:last-of-type {
        td {
          border-bottom: 1px solid;
          border-color: inherit;
          &:first-of-type {
            border-bottom-left-radius: 8px !important;
          }
          &:last-of-type {
            border-bottom-right-radius: 8px !important;
          }
        }
      }
    }
  }
}
.active-node-border-div {
  width: calc(100% + 12px);
  height: calc(100% + 12px);
  left: -6px;
  top: -6px;
  border: 4px solid colors.$primary;
  z-index: -1;
  opacity: 0.5;
}
</style>
