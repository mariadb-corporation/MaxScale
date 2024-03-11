<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
const props = defineProps({
  nodes: {
    type: Array,
    validator(arr) {
      const isArray = Array.isArray(arr)
      if (isArray && arr.length) return 'id' in arr[0]
      return isArray
    },
    required: true,
  },
  coordMap: { type: Object, default: () => ({}) }, //sync
  nodeStyle: { type: Object, default: () => ({}) },
  defNodeSize: { type: Object, required: true },
  draggable: { type: Boolean, default: false },
  autoWidth: { type: Boolean, default: false },
  revertDrag: { type: Boolean, default: false },
  boardZoom: { type: Number, required: true },
  hoverable: { type: Boolean, default: false },
  dblclick: { type: Boolean, default: false },
  contextmenu: { type: Boolean, default: false },
  click: { type: Boolean, default: false },
  clickOutside: { type: Boolean, default: false },
  clickedNodeId: { type: String, default: '' },
})

const emit = defineEmits([
  'update:coordMap',
  'update:clickedNodeId',
  'node-size-map', // size of nodes, keyed by node id
  'mouseenter',
  'mouseleave',
  'dblclick', // (node)
  'on-node-contextmenu', // ({e:Event, node:object})
  'click', // (node)
  'drag-start', // ({ e, node })
  'drag', // ({ e, node, diffX, diffY })
  'drag-end', // ({ e, node })
  'click-out-side',
])

const {
  lodash: { isEqual, cloneDeep },
  doubleRAF,
} = useHelpers()
const typy = useTypy()

const defDraggingStates = Object.freeze({
  isDragging: false,
  draggingNodeId: null,
  startCoord: null,
})

let dragEvent, dragEndEvent

let nodeSizeMap = ref({})
let draggingStates = ref(null)
let nodeRefs = ref([])

const nodeIds = computed(() => props.nodes.map((n) => n.id))
const nodeCoordMap = computed({
  get: () => props.coordMap,
  set: (v) => emit('update:coordMap', v),
})
const clickedGraphNodeId = computed({
  get: () => props.clickedNodeId,
  set: (v) => emit('update:clickedNodeId', v),
})

watch(
  nodeIds,
  (v, oV) => {
    // compute node height after nodes are rendered
    if (!isEqual(v, oV)) doubleRAF(() => setNodeSizeMap())
  },
  { deep: true, immediate: true }
)
watch(nodeSizeMap, (v) => emit('node-size-map', v), { deep: true })
watch(
  () => props.clickOutside,
  (v) => {
    if (v) document.addEventListener('click', onClickOutside)
    else document.removeEventListener('click', onClickOutside)
  },
  { immediate: true }
)

onBeforeMount(() => {
  if (props.draggable) setDefDraggingStates()
})
onBeforeUnmount(() => {
  if (props.clickOutside) document.removeEventListener('click', onClickOutside)
})

function handleAddEvents(node) {
  let events = {},
    isDblclick,
    dblclickTimeout,
    clickTimeout
  if (props.draggable) events.mousedown = (e) => dragStart({ e, node })
  if (props.hoverable && !draggingStates.value.isDragging) {
    events.mouseenter = (e) => emit('mouseenter', { e, node })
    events.mouseleave = (e) => emit('mouseleave', { e, node })
  }
  if (props.dblclick)
    events.dblclick = (e) => {
      isDblclick = true
      clearTimeout(dblclickTimeout)
      dblclickTimeout = setTimeout(() => (isDblclick = false), 200)
      resetClickedNodeId()
      e.stopPropagation()
      emit('dblclick', node)
    }
  if (props.contextmenu)
    events.contextmenu = (e) => {
      resetClickedNodeId()
      e.preventDefault()
      emit('on-node-contextmenu', { e, node })
    }
  if (props.click)
    events.click = (e) => {
      clearTimeout(clickTimeout)
      clickTimeout = setTimeout(() => {
        if (!isDblclick) {
          e.stopPropagation()
          clickedGraphNodeId.value = node.id
          emit('click', node)
        }
      }, 200)
    }
  return events
}

function setDefDraggingStates() {
  draggingStates.value = cloneDeep(defDraggingStates)
}

function setNodeSizeMap() {
  const graphNodes = typy(nodeRefs, 'value').safeArray
  let map = {}
  graphNodes.forEach((node) => {
    const { width, height } = getNodeEleSize(node)
    map[node.getAttribute('node_id')] = { width, height }
  })
  nodeSizeMap.value = map
}

/**
 * Calculates the size of an HTML element
 * The returned dimensions represent the value when the zoom ratio is 1.
 * @param {HTMLElement} node
 * @returns {{ width: number, height: number }}
 */
function getNodeEleSize(node) {
  const { width, height } = node.getBoundingClientRect()
  return { width: width / props.boardZoom, height: height / props.boardZoom }
}

function getPosStyle(id) {
  const { x = 0, y = 0 } = nodeCoordMap.value[id] || {}
  const { width, height } = getNodeSize(id)
  // center rectangular nodes
  return {
    left: `${x - width / 2}px`,
    top: `${y - height / 2}px`,
  }
}

function getNodeSizeStyle(id) {
  const { width } = getNodeSize(id)
  return {
    width: props.autoWidth ? 'unset' : `${width}px`,
    height: 'unset',
  }
}

/**
 * Handles the event when a node is resized.
 * @param {string} nodeId - Id of the node that was resized.
 */
function onNodeResized(nodeId) {
  // Run with doubleRAF to make sure getBoundingClientRect return accurate dim
  doubleRAF(() => {
    const nodeEle = typy(nodeRefs, 'value').safeArray.find(
      (n) => n.getAttribute('node_id') === nodeId
    )
    if (nodeEle) nodeSizeMap.value[nodeId] = getNodeEleSize(nodeEle)
  })
}

function addDragEvents(node) {
  /**
   * The handlers for mousemove and mouseup events are arrow functions which can't
   * be removed as they aren't attached to any variables.
   * This stores them to dragEvent and dragEndEvent so they can be later removed.
   */
  dragEvent = (e) => drag({ e, node })
  dragEndEvent = (e) => dragEnd({ e, node })
  document.addEventListener('mousemove', dragEvent)
  document.addEventListener('mouseup', dragEndEvent)
}

function rmDragEvents() {
  document.removeEventListener('mousemove', dragEvent)
  document.removeEventListener('mouseup', dragEndEvent)
}

function dragStart({ e, node }) {
  draggingStates.value = {
    ...draggingStates.value,
    draggingNodeId: node.id,
    startCoord: { x: e.clientX, y: e.clientY },
  }
  emit('drag-start', { e, node })
  addDragEvents(node)
}

function drag({ e, node }) {
  resetClickedNodeId()
  e.preventDefault()
  const { startCoord, draggingNodeId } = draggingStates.value
  if (startCoord && draggingNodeId === node.id) {
    const diffPos = { x: e.clientX - startCoord.x, y: e.clientY - startCoord.y }
    // calc offset position
    let diffX = diffPos.x / props.boardZoom,
      diffY = diffPos.y / props.boardZoom
    // update startCoord
    draggingStates.value = {
      ...draggingStates.value,
      isDragging: true,
      startCoord: { x: e.clientX, y: e.clientY },
    }

    //  graph is reverted, so minus offset
    if (props.revertDrag) {
      diffX = -diffX
      diffY = -diffY
    }
    const coord = nodeCoordMap.value[draggingNodeId]
    nodeCoordMap.value[draggingNodeId] = {
      x: coord.x + diffX,
      y: coord.y + diffY,
    }
    emit('drag', { e, node, diffX, diffY })
  }
}

function dragEnd(param) {
  emit('drag-end', param)
  rmDragEvents()
  setDefDraggingStates()
}

function resetClickedNodeId() {
  clickedGraphNodeId.value = ''
}

function onClickOutside() {
  if (clickedGraphNodeId.value) {
    emit('click-out-side')
    resetClickedNodeId()
  }
}

function getNodeSize(id) {
  return nodeSizeMap.value[id] || props.defNodeSize
}

defineExpose({ getNodeSize })
</script>

<template>
  <div class="svg-graph-nodes-ctr">
    <div
      v-for="node in nodes"
      ref="nodeRefs"
      :key="node.id"
      class="graph-node"
      :class="{ move: draggable, 'no-userSelect': draggingStates.isDragging }"
      :node_id="node.id"
      :style="{
        ...getPosStyle(node.id),
        ...nodeStyle,
        ...getNodeSizeStyle(node.id),
        zIndex: draggingStates.draggingNodeId === node.id ? 4 : 3,
      }"
      v-on="handleAddEvents(node)"
    >
      <slot
        :data="{
          node,
          nodeSize: getNodeSize(node.id),
          onNodeResized,
          isDragging: draggingStates.isDragging,
        }"
      />
    </div>
  </div>
</template>

<style lang="scss" scoped>
.svg-graph-nodes-ctr {
  top: 0;
  height: 0;
  width: 0;
  position: absolute;
  z-index: 3;
  .graph-node {
    position: absolute;
    background: transparent;
  }
}
</style>
