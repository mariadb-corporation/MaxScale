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
  loadChildren: { type: Function },
})
const emit = defineEmits(['update:expandedNodes', 'on-tree-changes'])

const HEADERS = [{ title: '', value: 'name' }]

const {
  delay,
  lodash: { isEqual, cloneDeep },
} = useHelpers()
const typy = useTypy()

let items = ref([])
let loadingNodeId = ref(null)
const loading = useLoading()

const expandedNodeIds = computed(() => props.expandedNodes.map((n) => n.id))

const tree = computed(() => items.value.filter((item) => item.level === 0))

watch(
  () => props.data,
  async (v, oV) => {
    if (!isEqual(v, oV) && !isEqual(v, tree.value)) {
      let nodes = cloneDeep(v)
      for (const id of expandedNodeIds.value) {
        const index = nodes.findIndex((n) => n.id === id)
        if (index !== -1) {
          const children = await handleLoadChildren(nodes[index])
          nodes[index].children = children
          nodes.splice(index, 1, ...[nodes[index], ...children])
        }
      }
      items.value = nodes
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
  return `${basePl + levelPl}px`
}
</script>

<template>
  <VDataTableVirtual
    :headers="HEADERS"
    :items="items"
    class="virtual-tree w-100"
    density="compact"
    item-value="key"
    :loading="loadingNodeId ? 'primary' : loading"
    :item-height="30"
  >
    <template #headers />
    <template #item="{ item: node, itemRef }">
      <VHover>
        <template #default="{ isHovering, props }">
          <tr :ref="itemRef" class="v-data-table__tr" v-bind="props">
            <td class="pointer" @click="hasChild(node) ? toggleNode(node) : null">
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
}
</style>
