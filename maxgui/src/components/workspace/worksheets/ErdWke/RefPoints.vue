<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { createShape } from '@/components/svgGraph/utils'
import { TARGET_POS } from '@/components/svgGraph/shapeConfig'

const props = defineProps({
  node: { type: Object, required: true },
  entitySizeConfig: { type: Object, required: true },
  linkContainer: { type: Object, required: true },
  boardZoom: { type: Number, required: true },
  graphConfig: { type: Object, required: true },
})
const emit = defineEmits(['drawing', 'draw-end'])

const { getAppEle } = useHelpers()

const draggingStates = ref(getDefDraggingStates())
const path = ref(null)

const POINT_RADIUS = 5

const shapeType = computed(() => props.graphConfig.linkShape.type)
const markerWidth = computed(() => props.graphConfig.marker.width)
const cols = computed(() => Object.values(props.node.data.defs.col_map))
const points = computed(() => [...getPoints(TARGET_POS.LEFT), ...getPoints(TARGET_POS.RIGHT)])

/**
 * @param {string} pointDirection - either TARGET_POS.RIGHT or TARGET_POS.LEFT
 */
function getPoints(pointDirection) {
  const { headerHeight, rowHeight: k } = props.entitySizeConfig
  return cols.value.map((c, i) => ({
    pos: {
      x: (pointDirection === TARGET_POS.RIGHT ? props.node.size.width : 0) - POINT_RADIUS,
      y: headerHeight + (k * i + k) - k / 2 - POINT_RADIUS,
    },
    col: c,
    pointDirection,
  }))
}

function addDragEvents() {
  document.addEventListener('mousemove', drawing)
  document.addEventListener('mouseup', drawEnd)
}

function rmDragEvents() {
  document.removeEventListener('mousemove', drawing)
  document.removeEventListener('mouseup', drawEnd)
}

function pathGenerator(data) {
  let targetPos = draggingStates.value.pointDirection
  if (targetPos === TARGET_POS.RIGHT && data.x1 < props.node.x)
    targetPos = TARGET_POS.INTERSECT_RIGHT
  else if (targetPos === TARGET_POS.LEFT && data.x1 > props.node.x)
    targetPos = TARGET_POS.INTERSECT_LEFT

  return createShape({
    type: shapeType.value,
    offset: markerWidth.value,
    data,
    targetPos,
  })
}

function dragStart({ e, point }) {
  const { col, pos, pointDirection } = point
  const { width, height } = props.node.size
  const offset = width / 2

  const x0 = props.node.x + (pointDirection === TARGET_POS.LEFT ? -offset : offset)
  const y0 = props.node.y - height / 2 + pos.y + POINT_RADIUS

  const startPoint = { x0, y0 }

  draggingStates.value = {
    srcAttrId: col.id,
    startClientPoint: { x: e.clientX, y: e.clientY },
    startPoint,
    pointDirection,
  }

  path.value = props.linkContainer
    .append('path')
    .attr('class', 'staging-link')
    .attr('fill', 'none')
    .attr('stroke-width', '1')
    .attr('stroke', '#0e9bc0')
    .attr('stroke-dasharray', '5')
    .attr('d', pathGenerator({ ...startPoint, x1: x0, y1: y0 }))
  addDragEvents()
}

function updatePath(diffPos) {
  const { startPoint } = draggingStates.value
  path.value.attr(
    'd',
    pathGenerator({
      x0: startPoint.x0,
      y0: startPoint.y0,
      x1: startPoint.x0 + diffPos.x,
      y1: startPoint.y0 + diffPos.y,
    })
  )
}

function drawing(e) {
  getAppEle().classList.add('cursor--crosshair--all')
  e.stopPropagation()
  const { startClientPoint } = draggingStates.value
  const diffPos = {
    x: (e.clientX - startClientPoint.x) / props.boardZoom,
    y: (e.clientY - startClientPoint.y) / props.boardZoom,
  }
  updatePath(diffPos)
  emit('drawing')
}

function drawEnd() {
  getAppEle().classList.remove('cursor--crosshair--all')
  emit('draw-end', {
    node: props.node,
    cols: [{ id: draggingStates.value.srcAttrId }],
  })
  path.value.remove()
  rmDragEvents()
  draggingStates.value = getDefDraggingStates()
}

function getDefDraggingStates() {
  return {
    srcAttrId: '',
    startClientPoint: null,
    startPoint: null,
    pointDirection: '',
  }
}
</script>

<template>
  <div class="ref-points">
    <!-- TODO: Add tooltip instruction -->
    <div
      v-for="(point, i) in points"
      :key="i"
      class="ref-point cursor--crosshair pos--absolute"
      :style="{
        width: `${POINT_RADIUS * 2}px`,
        height: `${POINT_RADIUS * 2}px`,
        borderRadius: '50%',
        backgroundColor: node.styles.highlightColor,
        top: `${point.pos.y}px`,
        left: `${point.pos.x}px`,
        zIndex: 5,
      }"
      @mousedown.stop="dragStart({ e: $event, point })"
    />
  </div>
</template>
