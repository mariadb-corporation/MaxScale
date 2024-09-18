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
import { forceSimulation, forceLink, forceCenter, forceCollide, forceX, forceY } from 'd3-force'
import GraphConfig from '@/components/svgGraph/GraphConfig'
import EntityLink from '@wkeComps/ErdWke/EntityLink'
import { EVENT_TYPES } from '@/components/svgGraph/linkConfig'
import { LINK_SHAPES } from '@/components/svgGraph/shapeConfig'
import { getConfig } from '@wkeComps/ErdWke/config'
import erdHelper from '@/utils/erdHelper'
import { CREATE_TBL_TOKEN_MAP } from '@/constants/workspace'
import EntityNodes from '@wkeComps/ErdWke/EntityNodes.vue'

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
  lodash: { cloneDeep, merge, keyBy },
  delay,
  deepDiff,
  getGraphExtent,
} = useHelpers()
const typy = useTypy()

const entityNodesRef = ref(null)
const isRendering = ref(false)
const linkContainer = ref({})
const graphNodes = ref([])
const graphLinks = ref([])
const simulation = ref(null)

const chosenLinks = ref([])
const entityLink = ref(null)
const graphConfig = ref(null)
const graphDim = ref({})

const hoveredLink = ref(null)
const tooltipX = ref(0)
const tooltipY = ref(0)

const panAndZoomData = computed({
  get: () => props.panAndZoom,
  set: (v) => emit('update:panAndZoom', v),
})

const entityKeyCategoryMap = computed(() =>
  graphNodes.value.reduce(
    (map, node) => ((map[node.id] = node.data.defs.key_category_map), map),
    {}
  )
)
const nodeMap = computed(() => keyBy(graphNodes.value, 'id'))

const isAttrToAttr = computed(() => typy(props.graphConfigData, 'link.isAttrToAttr').safeBoolean)

const isHighlightAll = computed(
  () => typy(props.graphConfigData, 'link.isHighlightAll').safeBoolean
)

const isStraightShape = computed(
  () => typy(props.graphConfigData, 'linkShape.type').safeString === LINK_SHAPES.STRAIGHT
)
const globalLinkColor = computed(() => typy(props.graphConfigData, 'link.color').safeString)

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
    entityNodesRef.value.onNodeResized(id)
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
 * Get the correct extent of nodes to control the zoom.
 */
function getExtent() {
  return getGraphExtent({ nodes: graphNodes.value, dim: graphDim.value })
}

/**
 * @public
 * @param {Array} nodes - erd nodes
 */
function update(nodes) {
  assignData(nodes)
  // setNodeSizeMap will trigger updateNodeSizes method
  nextTick(() => entityNodesRef.value.setSizeMap())
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
  typy(entityNodesRef.value, 'updateCoordMap').safeFunction()
  if (tickCount === 1) initLinkInstance()
  drawLinks()
  if (tickCount === 1) isRendering.value = false
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
    if (eventType === EVENT_TYPES.HOVER)
      highLightNodeLinks({ id: link.source.id, event: EVENT_TYPES.HOVER })
    else highLightNodeLinks({ event: EVENT_TYPES.NONE })
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

function setEventLinkStyles(eventType) {
  setEventStyles({ eventType, links: chosenLinks.value })
  drawLinks()
}

function highLightNodeLinks({ id, event }) {
  if (!isHighlightAll.value) {
    setEventLinkStyles(event)
    chosenLinks.value =
      event === EVENT_TYPES.NONE ? [] : erdHelper.getNodeLinks({ links: getLinks(), id })
  }
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

function getFkMap(node) {
  return typy(entityKeyCategoryMap.value, `[${node.id}][${CREATE_TBL_TOKEN_MAP.foreignKey}]`)
    .safeObjectOrEmpty
}

function getFks(node) {
  return Object.values(getFkMap(node))
}

function onDraggingNode({ id, diffX, diffY }) {
  // mutate each the node via nodeMap as nodeMap has a ref to graphNodes
  const nodeData = nodeMap.value[id]
  nodeData.x = nodeData.x + diffX
  nodeData.y = nodeData.y + diffY
  drawLinks()
}

defineExpose({ runSimulation, updateNode, addNode, getExtent, update })
</script>

<template>
  <div class="fill-height er-diagram">
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
        <EntityNodes
          ref="entityNodesRef"
          :style="style"
          :nodes="graphNodes"
          :graphConfigData="graphConfigData"
          :chosenLinks="chosenLinks"
          :boardZoom="panAndZoomData.k"
          :getFkMap="getFkMap"
          :activeNodeId="activeNodeId"
          :linkContainer="linkContainer"
          :colKeyCategoryMap="colKeyCategoryMap"
          :entityKeyCategoryMap="entityKeyCategoryMap"
          @node-size-map="updateNodeSizes"
          @highlight-node-links="highLightNodeLinks"
          @node-dragging="onDraggingNode"
          @node-dragend="$emit('on-node-drag-end', $event)"
          @on-node-contextmenu="$emit('on-node-contextmenu', $event)"
          @dblclick="$emit('dblclick', $event)"
          @on-create-new-fk="$emit('on-create-new-fk', $event)"
        >
          <template v-for="(_, name) in $slots" #[name]="slotData">
            <slot :name="name" v-bind="slotData" />
          </template>
        </EntityNodes>
      </template>
    </SvgGraphBoard>
    <VTooltip
      v-if="hoveredFkId"
      location="bottom"
      :activator="`#${hoveredFkId}`"
      :target="[tooltipX, tooltipY]"
    >
      <pre>{{ hoveredFkInfo }}</pre>
    </VTooltip>
  </div>
</template>
