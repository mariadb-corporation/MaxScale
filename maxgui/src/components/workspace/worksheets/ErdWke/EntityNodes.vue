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
import EntityNode from '@wkeComps/ErdWke/EntityNode.vue'
import RefPoints from '@wkeComps/ErdWke/RefPoints.vue'
import { REF_OPT_MAP } from '@/constants/workspace'
import { EVENT_TYPES } from '@/components/svgGraph/linkConfig'

defineOptions({ inheritAttrs: false })
const props = defineProps({
  nodes: { type: Object, required: true },
  graphConfigData: { type: Object, required: true },
  chosenLinks: { type: Array, required: true },
  boardZoom: { type: Number, required: true },
  getFkMap: { type: Function, required: true },
  activeNodeId: { type: String, required: true },
  linkContainer: { type: Object, required: true },
  colKeyCategoryMap: { type: Object, required: true },
  entityKeyCategoryMap: { type: Object, required: true },
})

const emit = defineEmits([
  'highlight-node-links',
  'node-dragging',
  'node-dragend',
  'on-create-new-fk',
])

const DEF_NODE_SIZE = { width: 250, height: 100 }
const { DRAGGING, HOVER } = EVENT_TYPES

const typy = useTypy()
const {
  lodash: { set },
  uuidv1,
  doubleRAF,
} = useHelpers()

const coordMap = ref({})
const ctrRef = ref(null)
const isDraggingNode = ref(false)
const clickedNodeId = ref('')
const clickOutside = ref(true)
const isDrawingFk = ref(false)
const refTarget = ref(null)

const entitySizeConfig = computed(
  () => typy(props.graphConfigData, 'linkShape.entitySizeConfig').safeObjectOrEmpty
)
const entityHighlightColStyleMap = computed(() =>
  props.chosenLinks.reduce((map, link) => {
    const {
      source,
      target,
      relationshipData: { src_attr_id, target_attr_id },
      styles: { invisibleHighlightColor },
    } = link

    if (!map[source.id]) map[source.id] = {}
    if (!map[target.id]) map[target.id] = {}
    const style = { backgroundColor: invisibleHighlightColor, color: 'white' }
    set(map, [source.id, src_attr_id], style)
    set(map, [target.id, target_attr_id], style)
    return map
  }, {})
)
const hoverable = computed(() => Boolean(!clickedNodeId.value))

watch(hoverable, (v) => {
  if (!v) emit('highlight-node-links', { event: EVENT_TYPES.NONE })
})

function onDragNode({ node, diffX, diffY }) {
  if (!isDraggingNode.value) emit('highlight-node-links', { id: node.id, event: DRAGGING })
  isDraggingNode.value = true
  emit('node-dragging', { id: node.id, diffX, diffY })
}

function onDragNodeEnd({ node }) {
  if (isDraggingNode.value) {
    emit('highlight-node-links', { event: EVENT_TYPES.NONE })
    isDraggingNode.value = false
    emit('node-dragend', node)
  }
}

function onDrawingFk() {
  clickOutside.value = false
  isDrawingFk.value = true
}

function onEndDrawFk({ node, cols }) {
  isDrawingFk.value = false
  if (refTarget.value) {
    const currentFkMap = props.getFkMap(node)
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
      on_delete: REF_OPT_MAP.NO_ACTION,
      on_update: REF_OPT_MAP.NO_ACTION,
    },
    node,
  }
}

/**
 * @public
 */
function updateCoordMap() {
  coordMap.value = props.nodes.reduce((map, n) => {
    const { x, y, id } = n
    map[id] = { x, y }
    return map
  }, {})
}

/**
 * @public
 */
function setSizeMap() {
  typy(ctrRef.value, 'setNodeSizeMap').safeFunction()
}

/**
 * @public
 * @param {string} id - node id
 */
function onNodeResized(id) {
  typy(ctrRef.value, 'onNodeResized').safeFunction(id)
}
defineExpose({ updateCoordMap, setSizeMap, onNodeResized })
</script>

<template>
  <div v-if="isDraggingNode" class="dragging-mask" data-test="dragging-mask" />
  <SvgGraphNodes
    ref="ctrRef"
    v-model:coordMap="coordMap"
    v-model:clickedNodeId="clickedNodeId"
    :nodes="nodes"
    :nodeStyle="{ userSelect: 'none' }"
    :defNodeSize="DEF_NODE_SIZE"
    draggable
    :hoverable="hoverable"
    :boardZoom="boardZoom"
    autoWidth
    dblclick
    contextmenu
    click
    :clickOutside="clickOutside"
    @drag="onDragNode($event)"
    @drag-end="onDragNodeEnd($event)"
    @mouseenter="emit('highlight-node-links', { id: $event.node.id, event: HOVER })"
    @mouseleave="emit('highlight-node-links', { event: EVENT_TYPES.NONE })"
    v-bind="$attrs"
  >
    <template #default="{ data: { node } }">
      <div
        v-if="node.id === activeNodeId"
        class="active-node-border-div pos--absolute rounded-lg"
        data-test="active-node-border-div"
      />
      <RefPoints
        v-if="node.id === clickedNodeId"
        :node="node"
        :entitySizeConfig="entitySizeConfig"
        :linkContainer="linkContainer"
        :boardZoom="boardZoom"
        :graphConfig="graphConfigData"
        @drawing="onDrawingFk()"
        @draw-end="onEndDrawFk($event)"
      />
      <EntityNode
        :node="node"
        :headerHeight="`${entitySizeConfig.headerHeight}px`"
        :rowHeight="`${entitySizeConfig.rowHeight}px`"
        :colKeyCategoryMap="colKeyCategoryMap"
        :keyCategoryMap="$typy(entityKeyCategoryMap[node.id]).safeObjectOrEmpty"
        :highlightColStyleMap="$typy(entityHighlightColStyleMap[node.id]).safeObjectOrEmpty"
        :isDrawingFk="isDrawingFk"
        @mouseenter-attr="setRefTargetData({ node, col: $event })"
        @mouseleave-attr="refTarget = null"
      >
        <template v-for="(_, name) in $slots" #[name]="slotData">
          <slot :name="name" v-bind="slotData" />
        </template>
      </EntityNode>
    </template>
  </SvgGraphNodes>
</template>

<style lang="scss" scoped>
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
