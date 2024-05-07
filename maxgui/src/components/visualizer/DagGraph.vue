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
import * as d3d from 'd3-dag'
import 'd3-transition'
import GraphConfig from '@/components/svgGraph/GraphConfig'
import Link from '@/components/svgGraph/Link'
import { getLinkConfig, EVENT_TYPES } from '@/components/svgGraph/linkConfig'

const props = defineProps({
  data: { type: Array, required: true },
  dim: { type: Object, required: true },
  defNodeSize: { type: Object, default: () => ({ width: 200, height: 100 }) },
  revert: { type: Boolean, default: false },
  colorizingLinkFn: { type: Function, default: () => '' },
  handleRevertDiagonal: { type: Function, default: () => false },
  draggable: { type: Boolean, default: false },
})

const typy = useTypy()
const {
  lodash: { isEqual, merge, cloneDeep },
} = useHelpers()

let linkContainer = null
let graphNodesRef = ref(null)
let graphDim = ref({ width: 0, height: 0 })
let graphNodeCoordMap = ref({})
let arrowHeadHeight = 12
let isDraggingNode = ref(false)
let chosenLinks = ref([])
let graphConfig = ref(null)
let dagLinks = ref([])
let panAndZoom = ref({ x: 0, y: 0, k: 1 })
let dag = null,
  linkInstance

const revertGraphStyle = computed(() => ({
  transform: props.revert ? 'rotate(180deg)' : 'rotate(0d)',
}))
const nodeIds = computed(() => props.data.map((n) => n.id))

// If the quantity of nodes changes, re-draw the graph.
watch(
  nodeIds,
  (v, oV) => {
    if (!isEqual(v, oV)) draw()
  },
  { deep: true }
)
onBeforeMount(() => initGraphConfig())

function initGraphConfig() {
  graphConfig.value = new GraphConfig(
    merge(getLinkConfig(), {
      link: {
        color: colorize,
        [EVENT_TYPES.HOVER]: { dashArr: '0' },
        [EVENT_TYPES.DRAGGING]: { dashArr: '0' },
      },
    })
  )
}

function onNodesRendered() {
  if (props.data.length) draw()
}

function draw() {
  initLinkInstance()
  computeDagLayout()
  drawLinks()
  setGraphNodeCoordMap()
}

function initLinkInstance() {
  linkInstance = new Link(graphConfig.value.config.link)
}

function computeDagLayout() {
  dag = d3d.dagStratify()(props.data)
  const layout = d3d
    .sugiyama() // base layout
    .layering(d3d.layeringSimplex())
    .decross(d3d.decrossTwoLayer()) // minimize number of crossings
    .coord(d3d.coordSimplex())
    .sugiNodeSize((d) => {
      const { width, height } = getDagNodeSize(d.data.node)
      // plus padding for each node as nodes are densely packed
      return [width + 20, height + 60]
    })

  const { width, height } = layout(dag)
  graphDim.value = { width, height }
  dagLinks.value = repositioning(dag.links())
}

function setGraphNodeCoordMap() {
  graphNodeCoordMap.value = dag.descendants().reduce((map, n) => {
    const {
      x,
      y,
      data: { id },
    } = n
    if (id) map[id] = { x, y }
    return map
  }, {})
}

/**
 * @param {Object} node - dag node
 * @returns {Object} - { width: Number, height: Number}
 */
function getDagNodeSize(node) {
  return graphNodesRef.value.getNodeSize(typy(node, 'data.id').safeString)
}

// Repositioning links so that links are drawn at the middle point of the edge
function repositioning(links) {
  return links.map((d) => {
    let shouldRevert = props.handleRevertDiagonal(d)
    const src = d.points[0]
    const target = d.points[d.points.length - 1]

    const srcSize = getDagNodeSize(d.source)
    const targetSize = getDagNodeSize(d.target)
    if (shouldRevert) {
      // src becomes a target point and vice versa
      src.y = src.y + srcSize.height / 2 + arrowHeadHeight
      target.y = target.y - targetSize.height / 2
    } else {
      src.y = src.y + srcSize.height / 2
      target.y = target.y - targetSize.height / 2 - arrowHeadHeight
    }
    return d
  })
}

/**
 * Handle override value for midPoint of param.points
 * @param {Object} param.points - Obtuse points to be overridden
 * @param {Boolean} param.isOpposite - is src node opposite to target node
 */
function setMidPoint({ points, isOpposite }) {
  if (isOpposite)
    points.midPoint = [points.targetX, points.srcY + (points.targetY - points.srcY) / 2]
  else points.midPoint = [(points.srcX + points.targetX) / 2, points.targetY]
}

/**
 * Handle override value for targetY, srcY, angle, midPoint of param.points
 * @param {Boolean} param.shouldRevert - result of handleRevertDiagonal()
 * @param {Object} param.src - source point
 * @param {Object} param.target - target point
 * @param {Object} param.sizes - {src,target} source and target node size
 * @param {Object} param.points - Obtuse points to be overridden
 */
function setOppositePoints({ shouldRevert, src, target, sizes, points }) {
  points.angle = shouldRevert ? 90 : 270
  points.srcY = shouldRevert ? src.y + sizes.src.height : src.y - sizes.src.height
  points.targetY = shouldRevert
    ? target.y - sizes.target.height - arrowHeadHeight * 2
    : target.y + sizes.target.height + arrowHeadHeight * 2

  if (shouldRevert) setMidPoint({ points, isOpposite: true })
}

/**
 * Handle override value for srcX, srcY, targetX, targetY, midPoint, angle, h of param.points
 * @param {Boolean} param.shouldRevert - result of handleRevertDiagonal()
 * @param {Object} param.src - source point
 * @param {Object} param.target - target point
 * @param {Object} param.sizes - {src,target} source and target node size
 * @param {Object} param.points - Obtuse points to be overridden
 */
function setSideBySidePoints({ shouldRevert, src, target, sizes, points }) {
  let isRightward = src.x > target.x + sizes.target.width,
    isLeftward = src.x < target.x - sizes.target.width

  // calc offset
  const srcXOffset = isRightward ? -sizes.src.width / 2 : sizes.src.width / 2
  const srcYOffset = shouldRevert ? sizes.src.height / 2 : -sizes.src.height / 2
  const targetXOffset = isRightward
    ? sizes.target.width / 2 + arrowHeadHeight - 2
    : -sizes.target.width / 2 - arrowHeadHeight
  const targetYOffset = shouldRevert
    ? -sizes.target.height / 2 - arrowHeadHeight
    : sizes.target.height / 2 + arrowHeadHeight

  if (isRightward || isLeftward) {
    // change coord of src point
    points.srcX = src.x + srcXOffset
    points.srcY = src.y + srcYOffset
    // change coord of target point
    points.targetX = target.x + targetXOffset
    points.targetY = target.y + targetYOffset
    setMidPoint({ points, isOpposite: false })
    points.angle = isRightward ? 180 : 0
    points.h = points.targetX
  }
}

function getObtusePoints(data) {
  let shouldRevert = props.handleRevertDiagonal(data)
  const dPoints = getPoints(data)
  const src = dPoints[0]
  const target = dPoints[dPoints.length - 1] // d3-dag could provide more than 2 points.
  const yGap = 24
  // set default points
  const points = {
    srcX: src.x,
    srcY: src.y,
    targetX: target.x,
    targetY: target.y,
    midPoint: [0, 0],
    h: target.x, // horizontal line from source to target,
    angle: shouldRevert ? 270 : 90,
  }
  setMidPoint({ points, isOpposite: true })

  let shouldChangeConnPoint = shouldRevert ? src.y - yGap <= target.y : src.y + yGap >= target.y

  if (shouldChangeConnPoint) {
    // get src and target node size
    const sizes = {
      src: getDagNodeSize(shouldRevert ? data.target : data.source),
      target: getDagNodeSize(shouldRevert ? data.source : data.target),
    }
    // Check if src node opposite to target node
    const isOpposite = shouldRevert
      ? src.y + sizes.src.height < target.y - sizes.target.height - yGap
      : src.y - sizes.src.height > target.y + sizes.target.height + yGap

    if (isOpposite) setOppositePoints({ shouldRevert, src, target, sizes, points })
    else setSideBySidePoints({ shouldRevert, src, target, sizes, points })
  }

  return points
}

/**
 * Creates a polyline between nodes where it draws from the source point
 * to the vertical middle point (middle point between source.y and target.y) as
 * a straight line. Then it draws from that midpoint to the source point which is
 * perpendicular to the source node
 * @param {Object} data - Link data
 */
function obtuseShape(data) {
  const { srcX, srcY, midPoint, h, targetX, targetY } = getObtusePoints(data)
  return `M ${srcX} ${srcY} ${midPoint} H ${h} L ${targetX} ${targetY}`
}

function getPoints(data) {
  let points = cloneDeep(data.points)
  let shouldRevert = props.handleRevertDiagonal(data)
  if (shouldRevert) points = points.reverse()
  return points
}

function pathGenerator(data) {
  return obtuseShape(data)
}

function transformArrow(data) {
  let { targetX, targetY, angle } = getObtusePoints(data)
  return `translate(${targetX}, ${targetY}) rotate(${angle})`
}
/**
 * @param {Object} d - link data or node data
 * @returns {String} - color
 */
function colorize(d) {
  return props.colorizingLinkFn(d) || '#0e9bc0'
}

/**
 * @param {Object} linkCtr - container element of the link
 * @param {String} joinType - enter or update
 */
function drawArrowHead({ linkCtr, joinType }) {
  const className = 'link__arrow'
  const transform = (d) => transformArrow(d)
  const opacity = (d) => linkInstance.getStyle(d, 'opacity')
  let arrowPaths
  switch (joinType) {
    case 'enter':
      arrowPaths = linkCtr.append('path').attr('class', className)
      break
    case 'update':
      arrowPaths = linkCtr.select(`path.${className}`)
      break
  }
  arrowPaths
    .attr('stroke-width', 3)
    .attr('d', 'M12,0 L-5,-8 L0,0 L-5,8 Z')
    .attr('stroke-linecap', 'round')
    .attr('stroke-linejoin', 'round')
    .attr('fill', colorize)
    .attr('transform', transform)
    .attr('opacity', opacity)
}

function handleMouseOverOut({ link, linkCtr, pathGenerator, eventType }) {
  linkInstance.setEventStyles({ links: [link], eventType })
  linkInstance.drawPaths({ linkCtr, joinType: 'update', pathGenerator })
  drawArrowHead({ linkCtr: linkCtr, joinType: 'update' })
}

function drawLinks() {
  linkInstance.drawLinks({
    containerEle: linkContainer,
    data: dagLinks.value,
    nodeIdPath: 'data.id',
    pathGenerator: pathGenerator,
    afterEnter: drawArrowHead,
    afterUpdate: drawArrowHead,
    events: {
      mouseover: (param) => handleMouseOverOut({ ...param, eventType: EVENT_TYPES.HOVER }),
      mouseout: (param) => handleMouseOverOut({ ...param, eventType: EVENT_TYPES.NONE }),
    },
  })
}

//-------------------------draggable methods---------------------------
/**
 *
 * @param {String} param.nodeId - id of the node has links being redrawn
 * @param {Number} param.diffX - difference of old coordinate x and new coordinate x
 * @param {Number} param.diffY - difference of old coordinate y and new coordinate y
 */
function updateLinkPositions({ nodeId, diffX, diffY }) {
  const dagNodes = dag.descendants()
  const dagNode = dagNodes.find((d) => d.data.id === nodeId)
  // change coord of child links
  for (const link of dagNode.ichildLinks()) {
    let point = link.points[0]
    point.x = point.x + diffX
    point.y = point.y + diffY
  }
  let parentLinks = []
  // change coord of links to parent nodes
  dagNode.data.parentIds.forEach((parentId) => {
    const parentNode = dagNodes.find((d) => d.data.id === parentId)
    const linkToParent = parentNode.childLinks().find((link) => link.target.data.id === nodeId)
    parentLinks.push(linkToParent)
    let point = linkToParent.points[linkToParent.points.length - 1]
    point.x = point.x + diffX
    point.y = point.y + diffY
  })
  // store links so that style applied to them can be reset to default after finish dragging
  chosenLinks.value = dagLinks.value.filter(
    (d) => d.source.data.id === nodeId || d.target.data.id === nodeId
  )
}

function setEventLinkStyles(eventType) {
  linkInstance.setEventStyles({ links: chosenLinks.value, eventType })
  drawLinks()
}

function onNodeDrag({ node, diffX, diffY }) {
  updateLinkPositions({ nodeId: node.id, diffX, diffY })
  if (!isDraggingNode.value) setEventLinkStyles(EVENT_TYPES.DRAGGING)
  isDraggingNode.value = true
  drawLinks()
}

function onNodeDragEnd() {
  if (isDraggingNode.value) setEventLinkStyles(EVENT_TYPES.NONE)
  isDraggingNode.value = false
}
</script>

<template>
  <SvgGraphBoard
    v-model="panAndZoom"
    class="dag-graph-container"
    :style="revertGraphStyle"
    :dim="dim"
    :graphDim="graphDim"
    @get-graph-ctr="linkContainer = $event"
  >
    <template #append="{ data: { style } }">
      <SvgGraphNodes
        v-model:coordMap="graphNodeCoordMap"
        ref="graphNodesRef"
        autoWidth
        :nodes="data"
        :style="style"
        :nodeStyle="revertGraphStyle"
        :defNodeSize="defNodeSize"
        draggable
        :revertDrag="revert"
        :boardZoom="panAndZoom.k"
        @node-size-map="onNodesRendered"
        @drag="onNodeDrag"
        @drag-end="onNodeDragEnd"
      >
        <template #default="{ data }">
          <slot name="graph-node-content" :data="data" />
        </template>
      </SvgGraphNodes>
    </template>
  </SvgGraphBoard>
</template>
