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
/**
 * A component for rendering key:value object
 */
import { objToTree } from '@/utils/dataTableHelpers'
import { watch } from 'vue'

const props = defineProps({
  data: { type: Object, required: true },
  showCellBorder: { type: Boolean, default: true },
  expandAll: { type: Boolean, default: false },
})

const emit = defineEmits(['get-flat-items'])

const headers = [
  { title: 'Variable', value: 'key', width: '1px', sortable: true },
  { title: 'Value', value: 'value', width: 'auto', sortable: true },
]
const {
  lodash: { cloneDeep },
} = useHelpers()
const typy = useTypy()

let items = ref([])

const tree = computed(() => {
  return objToTree({ obj: cloneDeep(props.data), level: 0 })
})
const flatItems = computed(() =>
  tree.value.flatMap((node) => expandNode({ node, recursive: true }))
)

watchEffect(() => {
  if (props.expandAll) items.value = flatItems.value
  else items.value = tree.value
})
watch(flatItems, (v) => emit('get-flat-items', v), { immediate: true })

/**
 * Return the node and its children
 * @param {object} param.node
 * @param {boolean} param.recursive
 * @returns {array}
 */
function expandNode({ node, recursive = false }) {
  let obj = cloneDeep(node)
  if (typy(obj, 'expanded').isDefined && obj.children) {
    obj.expanded = true
    let res = [obj]
    res.push(
      ...obj.children.flatMap((child) =>
        recursive ? expandNode({ node: child, recursive }) : child
      )
    )
    return res
  } else return [obj]
}

/**
 * Get all ids recursively from a given node
 * @param {object} node
 * @returns {array}
 */
function getIds(node) {
  let ids = [node.id]
  typy(node, 'children').safeArray.forEach((child) => ids.push(...getIds(child)))
  return ids
}

function toggleNode(node) {
  const index = items.value.findIndex((item) => item.id === node.id)
  if (index !== -1)
    if (node.expanded) {
      const childIds = getIds(node).slice(1)
      items.value = items.value.filter((node) => !childIds.includes(node.id))
      node.expanded = false
    } else items.value.splice(index, 1, ...expandNode({ node }))
}

function cellLevelPadding(cell) {
  const basePl = 8
  let levelPl = 30 * cell.level
  if (cell.leaf) levelPl += 40
  return {
    padding: `0px 48px 0px ${basePl + levelPl}px`,
  }
}

defineExpose({ headers })
</script>

<template>
  <VDataTable v-bind="{ ...$attrs }" :headers="headers" :items="items" :items-per-page="-1">
    <template #bottom>
      <!-- Declare an empty bottom slot to hide the footer -->
    </template>
    <template #[`item.key`]="{ item }">
      <div
        class="mxs-color-helper d-flex align-center fill-height override-def-padding"
        :class="{
          'border-right-table-border': showCellBorder,
          'font-weight-bold': item.expanded,
        }"
        :style="cellLevelPadding(item)"
      >
        <VBtn
          v-if="$typy(item, 'children').safeArray.length"
          width="32"
          height="32"
          class="mr-2"
          variant="text"
          icon
          @click="toggleNode(item)"
        >
          <VIcon
            :class="[item.expanded ? 'rotate-down' : 'rotate-right']"
            size="24"
            color="navigation"
            icon="$mdiChevronDown"
          />
        </VBtn>
        {{ item.key }}
      </div>
    </template>
    <template #[`item.value`]="{ item }">
      <div class="d-flex align-center fill-height">
        <slot name="item.value" :item="item">
          {{ String(item.value) }}
        </slot>
      </div>
    </template>
  </VDataTable>
</template>

<style lang="scss" scoped>
.override-def-padding {
  width: calc(100% + 48px);
  margin: 0px -24px;
  padding: 0px 24px;
}
</style>
