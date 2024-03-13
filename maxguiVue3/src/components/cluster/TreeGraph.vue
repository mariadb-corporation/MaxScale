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
import { select as d3Select } from 'd3-selection'
import { hierarchy, tree } from 'd3-hierarchy'
import 'd3-transition'
import { zoom, zoomIdentity } from 'd3-zoom'
import Sortable from 'sortablejs'

const props = defineProps({
  data: { type: Object, required: true },
  dim: { type: Object, required: true },
  draggable: { type: Boolean, default: false },
  draggableGroup: { type: Object, default: () => ({ name: 'TreeGraph' }) },
  cloneClass: { type: String, default: 'drag-node-clone' },
  noDragNodes: { type: Array, default: () => [] }, // list of node ids that are not draggable
  nodeSize: { type: Object, default: () => ({ width: 320, height: 100 }) },
  layoutConf: { type: Object, default: () => {} },
  expandedNodes: { type: Array, default: () => [] },
  nodeHeightMap: { type: Object, default: () => {} },
})

const emit = defineEmits([
  'on-node-drag-start', // Starts dragging a rect-node
  'on-node-dragging', // callback: (v: bool):void. If the callback returns true, it accepts the new position
  'on-node-drag-end', // Dragging ended
])

const DURATION = 300

const vSortable = {
  beforeMount: (el) => {
    const options = {
      swap: true,
      group: props.draggableGroup,
      draggable: props.draggable ? '.draggable-rect-node' : '',
      ghostClass: 'rect-node-ghost',
      animation: 0,
      forceFallback: true,
      fallbackClass: props.cloneClass,
      filter: '.no-drag',
      preventOnFilter: false,
      onStart: (e) => emit('on-node-drag-start', e),
      onMove: (e) => {
        emit('on-node-dragging', e)
        return false // cancel drop
      },
      onEnd: (e) => emit('on-node-drag-end', e),
    }
    Sortable.create(el, options)
  },
}

const {
  lodash: { merge },
} = useHelpers()

let nodeDivData = ref([])
let root = ref({})
let nodeGroupTransform = ref({ x: 24, y: props.dim.height / 2, k: 1 })
let nodeSizeChangesCount = ref(0)
let svgRef = ref(null)
let d3Svg, svgGroup

const layout = computed(() => merge({ margin: { left: 24 } }, props.layoutConf))
const treeLayout = computed(() => tree().size([props.dim.height, props.dim.width]))

watch(
  () => props.data,
  (v) => {
    computeHrchyLayout(v)
    update(root.value)
  },
  { deep: true }
)
watch(
  () => props.nodeSize,
  (v) => {
    if (nodeSizeChangesCount.value === 0) {
      nodeSizeChangesCount.value += 1
      // center root node should be run once
      nodeGroupTransform.value['y'] = -v.height / 2
    }
    update(root.value)
  }
)
onMounted(() => {
  if (props.data) {
    computeHrchyLayout(props.data)
    initSvg()
    update(root.value)
  }
})

/**
 * compute a hierarchical layout
 * @param {object} data - tree data
 */
function computeHrchyLayout(data) {
  root.value = hierarchy(data)
  // vertically center root node
  root.value['x0'] = props.dim.height / 2
  root.value['y0'] = 0
}

function initSvg() {
  const { left } = layout.value.margin
  nodeGroupTransform.value = zoomIdentity.translate(left, -props.nodeSize.height / 2).scale(1)
  d3Svg = d3Select(svgRef.value)
    .call(zoom().transform, nodeGroupTransform.value)
    .call(zoom().on('zoom', (e) => (nodeGroupTransform.value = e.transform)))
    .on('dblclick.zoom', null)
  svgGroup = d3Svg.select('g#node-group')
}

/**
 * @param {object} param.node - node to be vertically centered
 * @param {object} param.recHeight - height of the svg rect
 * @param {object} param.divHeight - height of the rectangular div
 * @returns {string} px string
 */
function centerNode({ node, rectHeight, divHeight }) {
  return `${node.x + (rectHeight - divHeight) / 2}px`
}

/**
 * @param {object} node - node to be vertically centered
 */
function handleCenterRectNodeVert(node) {
  let res = `${node.x}px`
  if (!props.expandedNodes.length) return res
  else {
    const rectHeight = props.nodeSize.height
    const divHeight = props.nodeHeightMap[node.id]
    if (rectHeight > divHeight) res = centerNode({ node, rectHeight, divHeight })
  }
  return res
}

/**
 * Return cubic bezier point to create a bezier curve line from source node to dest node
 * @param {object} param.dest - hierarchy d3 destination node
 * @param {object} param.src - hierarchy d3 source node
 * @returns {object} - returns { start: [x0, y0], p1: [x1, y1], p2: [x2, y2], end: [x, y] }
 * start point cord: x0,y0
 * control point 1 cord: x1,y1
 * control point 2 cord: x2,y2
 * end point cord: x,y
 */
function getCubicBezierPoints({ dest, src }) {
  // since the graph is draw horizontally, node.y is x0 and node.x is y0
  let x0 = src.y + props.nodeSize.width,
    y0 = src.x + props.nodeSize.height / 2,
    x = dest.y, // ending point
    y = dest.x + props.nodeSize.height / 2, // ending point
    // curves
    x1 = (x0 + x) / 2,
    x2 = x1,
    y1 = y0,
    y2 = y
  return { start: [x0, y0], p1: [x1, y1], p2: [x2, y2], end: [x, y] }
}

/**
 * De Casteljau's algorithm
 * B(t) = (1 - t)^3*P0 + 3(1 - t)^2*t*P1 + 3(1 - t)t^2*P2 + t^3*P3
 * @param {array} param.start - starting point cord [x,y] (P0)
 * @param {array} param.p1 - point 1 cord [x,y] (P1)
 * @param {array} param.p2 - point 2 cord [x,y] (P2)
 * @param {array} param.end - ending point cord [x,y] (P3)
 * @returns {function} - returns interpolator function which returns a point coord {x,y} at t
 */
function interpolateCubicBezier({ start, p1, p2, end }) {
  /**
   * @param {number} t - arbitrary parameter value 0 <= t <= 1
   * @returns {object} - point coord {x,y}
   */
  return (t) => ({
    x:
      Math.pow(1 - t, 3) * start[0] +
      3 * Math.pow(1 - t, 2) * t * p1[0] +
      3 * (1 - t) * Math.pow(t, 2) * p2[0] +
      Math.pow(t, 3) * end[0],
    y:
      Math.pow(1 - t, 3) * start[1] +
      3 * Math.pow(1 - t, 2) * t * p1[1] +
      3 * (1 - t) * Math.pow(t, 2) * p2[1] +
      Math.pow(t, 3) * end[1],
  })
}

/**
 * B'(t) = 3(1- t)^2(P1 - P0) + 6(1 - t)t(P2 - P1) + 3t^2(P3 - P2)
 * https://en.wikipedia.org/wiki/B%C3%A9zier_curve
 * @param {array} param.start - starting point cord [x,y] (P0)
 * @param {array} param.p1 - point 1 cord [x,y] (P1)
 * @param {array} param.p2 - point 2 cord [x,y] (P2)
 * @param {array} param.end - ending point cord [x,y] (P3)
 * @returns {function} - returns interpolator function which returns a point coord {x,y}
 */
function interpolateAngle({ start, p1, p2, end }) {
  /**
   * @param {number} t - arbitrary parameter value 0 <= t <= 1
   * @returns {number} - returns angle of the point
   */
  return function interpolator(t) {
    const tangentX =
      3 * Math.pow(1 - t, 2) * (p1[0] - start[0]) +
      6 * (1 - t) * t * (p2[0] - p1[0]) +
      3 * Math.pow(t, 2) * (end[0] - p2[0])
    const tangentY =
      3 * Math.pow(1 - t, 2) * (p1[1] - start[1]) +
      6 * (1 - t) * t * (p2[1] - p1[1]) +
      3 * Math.pow(t, 2) * (end[1] - p2[1])

    return Math.atan2(tangentY, tangentX) * (180 / Math.PI)
  }
}

/**
 *
 * @param {object} param.dest - hierarchy d3 destination node
 * @param {object} param.src - hierarchy d3 source node
 * @param {number} param.numOfPoints - Divide a single Cubic Bézier curves into number of points
 * @param {number} param.pointIdx - index of point to be returned
 * @returns {object} - obj point at provided pointIdx {position, angle}
 */
function getRotatedPoint({ dest, src, numOfPoints, pointIdx }) {
  const cubicBezierPoints = getCubicBezierPoints({ dest, src })
  const cubicInterpolator = interpolateCubicBezier(cubicBezierPoints)
  const cubicAngleInterpolator = interpolateAngle(cubicBezierPoints)
  const t = pointIdx / (numOfPoints - 1)
  return {
    position: cubicInterpolator(t),
    angle: cubicAngleInterpolator(t),
  }
}

/**
 * Creates a Cubic Bézier curves path from source node to the destination nodes
 * @param {object} param.dest - hierarchy d3 destination node
 * @param {object} param.src - hierarchy d3 source node
 */
function diagonal({ dest, src }) {
  const { start, p1, p2, end } = getCubicBezierPoints({ dest, src })
  return `M ${start} C ${p1}, ${p2}, ${end}`
}

/**
 * Toggle node on click.
 * @param {object} node - hierarchy d3 node
 */
function onNodeClick(node) {
  if (node.children) {
    //collapse
    node._children = node.children
    node.children = null
  } else {
    // expand
    node.children = node._children
    node._children = null
  }
  update(node)
}

/**
 * @param {object} srcNode - source node
 * @param {object} linkGroup - linkGroup
 *  @param {string} type - enter, update or exit
 */
function drawLine({ srcNode, linkGroup, type }) {
  const className = 'link_line'
  const strokeWidth = 2.5
  switch (type) {
    case 'enter':
      linkGroup
        .append('path')
        .attr('class', className)
        .attr('fill', 'none')
        .attr('stroke-width', strokeWidth)
        .attr('stroke', (d) => d.data.linkColor)
        .attr('d', () =>
          diagonal({
            // start at the right edge of the rect node
            dest: { x: srcNode.x0, y: srcNode.y0 + props.nodeSize.width },
            src: { x: srcNode.x0, y: srcNode.y0 },
          })
        )
      break
    case 'update':
      linkGroup.select(`path.${className}`).attr('d', (d) => diagonal({ dest: d, src: d.parent }))
      break
    case 'exit':
      linkGroup.select(`path.${className}`).attr('d', () =>
        diagonal({
          // end at the right edge of the rect node
          dest: { x: srcNode.x, y: srcNode.y + props.nodeSize.width },
          src: srcNode,
        })
      )
      break
  }
}

/**
 * @param {object} srcNode - source node
 * @param {object} linkGroup - linkGroup
 * @param {string} type - enter, update or exit
 */
function drawArrowHead({ srcNode, linkGroup, type }) {
  const className = 'link__arrow'
  switch (type) {
    case 'enter':
      linkGroup
        .append('path')
        .attr('class', className)
        .attr('stroke-width', 3)
        .attr('d', 'M12,0 L-5,-8 L0,0 L-5,8 Z')
        .attr('stroke-linecap', 'round')
        .attr('stroke-linejoin', 'round')
        .attr('transform', () => {
          let o = {
            x: srcNode.x0,
            // start at the right edge of the rect node
            y: srcNode.y0 + props.nodeSize.width,
          }
          const p = getRotatedPoint({
            dest: o,
            src: { x: srcNode.x0, y: srcNode.y0 },
            numOfPoints: 10,
            pointIdx: 0,
          })
          return `translate(${p.position.x}, ${p.position.y})`
        })

      break
    case 'update':
      linkGroup
        .select(`path.${className}`)
        .attr('fill', (d) => d.data.linkColor)
        .attr('transform', (d) => {
          const p = getRotatedPoint({
            dest: d,
            src: d.parent,
            numOfPoints: 10,
            pointIdx: 7, // show arrow at point 7
          })
          return `translate(${p.position.x}, ${p.position.y}) rotate(${p.angle})`
        })
      break
    case 'exit':
      linkGroup
        .select(`path.${className}`)
        .attr('fill', 'transparent')
        .attr('transform', () => {
          const p = getRotatedPoint({
            dest: {
              x: srcNode.x, // end at the right edge of the rect node
              y: srcNode.y + props.nodeSize.width,
            },
            src: srcNode,
            numOfPoints: 10,
            pointIdx: 0,
          })
          return `translate(${p.position.x}, ${p.position.y})`
        })
      break
  }
}

function drawArrowLink(param) {
  drawLine(param)
  drawArrowHead(param)
}

function drawLinks({ srcNode, links }) {
  // Update the links...
  let linkGroup
  svgGroup
    .selectAll('.link-group')
    .data(links, (d) => d.id)
    .join(
      (enter) => {
        // insert after .node
        linkGroup = enter.insert('g', 'g.node').attr('class', 'link-group')
        drawArrowLink({ srcNode, linkGroup, type: 'enter' })
        return linkGroup
      },
      // update is called when node changes it size
      (update) => {
        linkGroup = update.merge(linkGroup).transition().duration(DURATION)
        drawArrowLink({ srcNode, linkGroup, type: 'update' })
        return linkGroup
      },
      (exit) => {
        let linkGroup = exit.transition().duration(DURATION).remove()
        drawArrowLink({ srcNode, linkGroup, type: 'exit' })
        return linkGroup
      }
    )
}
/**
 * This handles add a padding of 10px to x property of a node
 * which represents y cord as the graph is render horizontally
 * @param {array} siblings - siblings nodes
 */
function collision(siblings) {
  let minPadding = 10
  if (siblings) {
    for (let i = 0; i < siblings.length - 1; i++) {
      if (siblings[i + 1].x - (siblings[i].x + props.nodeSize.height) < minPadding)
        siblings[i + 1].x = siblings[i].x + props.nodeSize.height + minPadding
    }
  }
}

/**
 * Update node
 * @param {object} srcNode - hierarchy d3 node to be updated
 */
function update(srcNode) {
  // Recompute x,y coord for all nodes
  let treeData = treeLayout.value(root.value)
  // Compute the new tree layout.
  let nodes = treeData.descendants(),
    links = nodes.slice(1)
  breadthFirstTraversal(nodes, collision)
  // Normalize for fixed-depth.
  nodes.forEach((node) => {
    node.y = node.depth * (props.nodeSize.width * 1.5)
    node.id = node.data.name
  })
  drawLinks({ srcNode, links })
  // Store the old positions for transition.
  nodes.forEach((d) => {
    d.x0 = d.x
    d.y0 = d.y
  })
  nodeDivData.value = nodes
}

/**
 *  Breadth-first traversal of the tree
 * cb function is processed on every node of a same level
 * @param {array} nodes - flatten tree nodes
 * @param {function} cb
 * @returns {number} - max number of nodes on a the same level
 */
function breadthFirstTraversal(nodes, cb) {
  let max = 0
  if (nodes && nodes.length > 0) {
    let currentDepth = nodes[0].depth
    let fifo = []
    let currentLevel = []

    fifo.push(nodes[0])
    while (fifo.length > 0) {
      let node = fifo.shift()
      if (node.depth > currentDepth) {
        cb(currentLevel)
        currentDepth++
        max = Math.max(max, currentLevel.length)
        currentLevel = []
      }
      currentLevel.push(node)
      if (node.children) {
        for (let j = 0; j < node.children.length; j++) {
          fifo.push(node.children[j])
        }
      }
    }
    cb(currentLevel)
    return Math.max(max, currentLevel.length)
  }
  return 0
}
</script>

<template>
  <!-- TODO: Use SvgGraphBoard -->
  <div class="tree-graph-container fill-height">
    <VIcon class="svg-grid-bg" color="card-border-color" icon="mxs:gridBg" />
    <svg ref="svgRef" class="tree-graph" :width="dim.width" height="100%">
      <g
        id="node-group"
        :transform="`translate(${nodeGroupTransform.x},
                ${nodeGroupTransform.y}) scale(${nodeGroupTransform.k})`"
      />
    </svg>
    <div
      v-sortable
      class="node-div-wrapper"
      :style="{
        transform: `translate(${nodeGroupTransform.x}px,
                ${nodeGroupTransform.y}px) scale(${nodeGroupTransform.k})`,
      }"
    >
      <div
        v-for="node in nodeDivData"
        :key="node.id"
        class="rect-node"
        :node_id="node.id"
        :class="{
          'draggable-rect-node': draggable,
          'no-drag': noDragNodes.includes(node.id),
        }"
        :style="{
          top: handleCenterRectNodeVert(node),
          left: `${node.y}px`,
        }"
      >
        <div
          v-if="node.children || node._children"
          class="node__circle node__circle--clickable"
          :style="{
            border: `1px solid ${node.data.linkColor}`,
            background: !node.children ? node.data.linkColor : 'white',
          }"
          @click="onNodeClick(node)"
        />
        <slot :data="{ node }" />
      </div>
    </div>
  </div>
</template>

<style lang="scss" scoped>
.tree-graph-container {
  width: 100%;
  position: relative;
  overflow: hidden;
  .svg-grid-bg {
    width: 100%;
    height: 100%;
    z-index: 1;
    pointer-events: none;
    background: transparent;
    position: absolute;
    left: 0;
  }
  .tree-graph {
    position: relative;
    left: 0;
    z-index: 2;
  }
  .node-div-wrapper {
    top: 0;
    height: 0;
    width: 0;
    position: absolute;
    z-index: 3;
    .rect-node:not(.drag-node-clone) {
      position: absolute;
      background: transparent;
      .node__circle {
        position: absolute;
        z-index: 4;
        top: calc(50% + 7px);
        left: 0;
        transform: translate(-50%, -100%);
        width: 14px;
        height: 14px;
        border-radius: 50%;
        transition: all 0.1s linear;
        &--clickable {
          cursor: pointer;
          left: unset;
          right: 0;
          transform: translate(50%, -100%);
          &:hover {
            width: 16.8px;
            height: 16.8px;
          }
        }
      }
    }
  }
}
.draggable-rect-node:not(.no-drag) {
  cursor: move;
}

.rect-node-ghost {
  background: colors.$tr-hovered-color !important;
  opacity: 0.6;
}
.drag-node-clone {
  opacity: 1 !important;
}
</style>
