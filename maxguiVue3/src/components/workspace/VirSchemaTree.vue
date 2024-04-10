<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { LOADING_TIME } from '@/constants'

const props = defineProps({
  data: { type: Object, required: true },
  expandedNodes: { type: Array, default: () => [] },
  selectedNodes: { type: Array },
  loadChildren: { type: Function },
  hasNodeCtxEvt: { type: Boolean, default: false },
  hasDbClickEvt: { type: Boolean, default: false },
  /**
   * The key property is used as the item-value for VDataTableVirtual as it
   * guarantees the uniqueness of the node. However, in most cases, the id
   * property can be used to detect the active node, as the only case where
   * id is not unique is when a node is part of a composite index.
   */
  activeNode: { type: Object, default: () => ({}) },
})
const emit = defineEmits([
  'update:expandedNodes',
  'update:selectedNodes',
  'on-tree-changes',
  'node:contextmenu',
  'node:dblclick',
])

const HEADERS = [{ title: '', value: 'name' }]

const {
  delay,
  lodash: { isEqual, cloneDeep, xorWith },
  ciStrIncludes,
  immutableUpdate,
} = useHelpers()
const typy = useTypy()

let items = ref([])
let loadingNodeId = ref(null)
let clickTimeout = ref(null)
let dblclickTimeout = ref(null)
let isDblclick = ref(false)
let initializeLoading = ref(false)
const loading = useLoading()

const expandedNodeIds = computed(() => props.expandedNodes.map((n) => n.id))
const tree = computed(() => cloneDeep(items.value.filter((item) => item.level === 0)))
const selectable = computed(() => typy(props.selectedNodes).isDefined)
const selectedNodeIds = computed(() => typy(props.selectedNodes).safeArray.map((n) => n.id))

watch(
  () => props.data,
  async (v, oV) => {
    if (!isEqual(v, oV) && !isEqual(v, tree.value)) {
      let nodes = cloneDeep(v)
      initializeLoading.value = 'primary' // for vuetify loading color
      for (const id of expandedNodeIds.value) {
        const index = nodes.findIndex((n) => n.id === id)
        if (index !== -1) {
          const children = await handleLoadChildren(nodes[index])
          nodes[index].children = children
          nodes.splice(index, 1, ...[nodes[index], ...children])
        }
      }
      items.value = nodes
      initializeLoading.value = false
    }
  },
  { deep: true, immediate: true }
)
watch(
  tree,
  (v, oV) => {
    if (!isEqual(v, oV)) emit('on-tree-changes', v)
  },
  { deep: true }
)

function isExpanded(id) {
  return expandedNodeIds.value.includes(id)
}

/**
 * Get all offspring ids recursively from a given node
 * @param {object} node
 * @returns {array}
 */
function getOffspringIds(node) {
  let offspringIds = []
  for (let child of typy(node, 'children').safeArray)
    offspringIds.push(child.id, ...getOffspringIds(child))
  return offspringIds
}

/**
 * Collapse the node by filtering out its children nodes from flat items.
 * @param {array} offspringIds
 */
function collapseNode(offspringIds) {
  items.value = items.value.filter((item) => !offspringIds.includes(item.id))
}

/**
 * Return the node and its children
 * @param {object} node
 * @returns {promise<array>}
 */
async function handleLoadChildren(node) {
  let children = node.children
  if (typy(props.loadChildren).isDefined && !node.children.length) {
    loadingNodeId.value = node.id
    await delay(LOADING_TIME)
    children = await props.loadChildren(node)
    loadingNodeId.value = null
  }
  return children
}

/**
 * Load the children of the node, mutate the children property
 * then merge them all to flat items.
 * @param {object} node
 * @returns {promise<void>}
 */
async function expandNode(node) {
  const children = await handleLoadChildren(node)
  node.children = children
  const itemIdx = items.value.findIndex((n) => n.id === node.id)
  items.value.splice(itemIdx, 1, ...[node, ...children])
}

async function toggleNode(node) {
  if (isExpanded(node.id)) {
    const offspringIds = getOffspringIds(node)
    collapseNode(offspringIds)
    emit(
      'update:expandedNodes',
      props.expandedNodes.filter((n) => !offspringIds.includes(n.id) && n.id !== node.id)
    )
  } else {
    await expandNode(node)
    emit('update:expandedNodes', [...props.expandedNodes, node])
  }
}

function hasChild(node) {
  return typy(node, 'children').isDefined
}

function levelPadding(node) {
  const basePl = 8
  let levelPl = 16 * node.level
  if (!hasChild(node)) levelPl += 4
  if (node.level !== 0 && selectable.value) levelPl += 8
  return `${basePl + levelPl}px`
}

/**
 * Checks if the tree contains a node with a name matching the given query.
 * @param {Object} options.node - The current node to check.
 * @param {string} options.query - The query to search for.
 * @returns {boolean} - True if the tree contains a matching node, false otherwise.
 */
function treeContainsName({ node, query }) {
  if (ciStrIncludes(node.name, query)) return true
  for (const child of typy(node, 'children').safeArray)
    if (treeContainsName({ node: child, query })) return true
  return false
}

function filterNode(_, query, item) {
  return treeContainsName({ node: item.raw, query })
}

function onClickNode(node) {
  if (hasChild(node)) {
    clearTimeout(clickTimeout.value)
    clickTimeout.value = setTimeout(() => {
      if (!isDblclick.value) {
        toggleNode(node)
      }
    }, 200)
  }
}

function onNodeCtxMenu(e, node) {
  if (props.hasNodeCtxEvt) {
    e.preventDefault()
    emit('node:contextmenu', node)
  }
}

function onNodeDblclick(node) {
  if (props.hasDbClickEvt) {
    isDblclick.value = true
    clearTimeout(dblclickTimeout.value)
    dblclickTimeout.value = setTimeout(() => (isDblclick.value = false), 200)
    emit('node:dblclick', node)
  }
}

function isSelected(node) {
  if (hasChild(node)) {
    const offspringIds = getOffspringIds(node)
    if (!offspringIds.length) return selectedNodeIds.value.includes(node.id)
    return offspringIds.every((id) => selectedNodeIds.value.includes(id))
  }
  return selectedNodeIds.value.includes(node.id)
}

function getIndeterminateValue(node) {
  if (hasChild(node)) {
    const offspringIds = getOffspringIds(node)
    if (!offspringIds.length) return false
    return isSelected(node) ? false : offspringIds.some((id) => selectedNodeIds.value.includes(id))
  }
  return false
}

/**
 * For now, only leaf selection is supported
 * @param {boolean} param.v
 * @param {object} param.node
 */
async function toggleSelect({ v, node }) {
  if (hasChild(node)) {
    let children = node.children
    if (v) {
      if (!node.children.length) {
        children = await handleLoadChildren(node)
        const itemIdx = items.value.findIndex((n) => n.id === node.id)
        items.value.splice(itemIdx, 1, immutableUpdate(node, { children: { $set: children } }))
      }
      emit(
        'update:selectedNodes',
        immutableUpdate(props.selectedNodes, {
          $push: children.length ? children : [node], // if childless, select the node
        })
      )
    } else {
      emit(
        'update:selectedNodes',
        xorWith(
          props.selectedNodes,
          // remove also childless node
          selectedNodeIds.value.includes(node.id) ? [node, ...children] : children,
          isEqual
        )
      )
    }
  } else
    emit(
      'update:selectedNodes',
      v
        ? immutableUpdate(props.selectedNodes, { $push: [node] })
        : props.selectedNodes.filter((n) => n.id !== node.id)
    )
}
defineExpose({ toggleNode })
</script>

<template>
  <VDataTableVirtual
    :headers="HEADERS"
    :items="items"
    class="virtual-tree w-100"
    density="compact"
    item-value="key"
    :loading="initializeLoading || loading"
    :item-height="30"
    :custom-filter="filterNode"
  >
    <template #headers />
    <template #item="{ item: node, itemRef }">
      <VHover>
        <template #default="{ isHovering, props }">
          <tr
            :ref="itemRef"
            :id="`node-${node.key}`"
            class="v-data-table__tr"
            :class="{ 'tr--active': $typy(activeNode, 'id').safeString === node.id }"
            v-bind="props"
          >
            <td
              class="cursor--pointer"
              @click="onClickNode(node)"
              @contextmenu="onNodeCtxMenu($event, node)"
              @dblclick="onNodeDblclick(node)"
            >
              <div
                class="d-flex align-center pr-2 fill-height"
                :style="{ paddingLeft: levelPadding(node) }"
              >
                <VProgressCircular
                  v-if="loadingNodeId === node.id"
                  size="16"
                  class="ml-1"
                  color="primary"
                  width="2"
                  indeterminate
                />
                <VBtn
                  v-else-if="hasChild(node)"
                  variant="text"
                  density="compact"
                  size="small"
                  icon
                  @click.stop="toggleNode(node)"
                >
                  <VIcon
                    :class="[isExpanded(node.id) ? 'rotate-down' : 'rotate-right']"
                    color="navigation"
                    icon="$mdiChevronDown"
                  />
                </VBtn>
                <VCheckboxBtn
                  v-if="selectable"
                  :modelValue="isSelected(node)"
                  :indeterminate="getIndeterminateValue(node)"
                  density="compact"
                  inline
                  @update:modelValue="toggleSelect({ v: $event, node })"
                  @click.stop
                />
                <slot name="label" :node="node" :isHovering="isHovering">
                  <span class="ml-1 d-inline-block text-truncate">
                    {{ node.name }}
                  </span>
                </slot>
              </div>
            </td>
          </tr>
        </template>
      </VHover>
    </template>
    <template #bottom />
  </VDataTableVirtual>
</template>

<style lang="scss" scoped>
.virtual-tree {
  :deep(table) {
    table-layout: fixed;
  }
  :deep(td) {
    border: none !important;
    padding: 0 !important;
    font-size: 0.75rem;
  }
  :deep(.v-table__wrapper) {
    overflow-x: hidden;
  }
  .tr--active {
    background: colors.$selected-tr-color;
  }
}
</style>
