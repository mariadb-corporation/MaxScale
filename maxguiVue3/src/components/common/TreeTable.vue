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
import { objToTree } from '@/utils/treeTableHelpers'

const props = defineProps({
  data: { type: Object, required: true },
  showCellBorder: { type: Boolean, default: true },
  expandAll: { type: Boolean, default: false },
  keyWidth: { type: [String, Number], default: '1px' },
  valueWidth: { type: [String, Number], default: 'auto' },
  keyInfoMap: { type: Object, default: () => ({}) },
  showKeyLength: { type: Boolean, default: false },
  arrayTransform: { type: Boolean, default: true },
  hideHeader: { type: Boolean, default: false },
})

const emit = defineEmits(['get-nodes'])

const headers = [
  {
    title: 'Variable',
    value: 'key',
    headerProps: {
      style: { width: props.keyWidth },
    },
    cellProps: {
      class: ['pa-0', props.showCellBorder ? 'mxs-color-helper border-right-table-border' : ''],
    },
  },
  {
    title: 'Value',
    value: 'value',
    headerProps: {
      style: { width: props.valueWidth },
    },
    cellProps: {
      class: 'pa-0',
      style: {
        // set maxWidth to 1px to activate auto truncation
        maxWidth: '1px',
      },
    },
  },
]
const {
  lodash: { cloneDeep, groupBy },
} = useHelpers()
const typy = useTypy()

let items = ref([])
const { sortBy, toggleSortBy, compareFn } = useSortBy({ key: 'key', isDesc: false })

const tree = computed(() => {
  return objToTree({ obj: cloneDeep(props.data), level: 0, arrayTransform: props.arrayTransform })
})
const flatItems = computed(() =>
  tree.value.flatMap((node) => expandNode({ node, recursive: true }))
)
const hasChild = computed(() => flatItems.value.some((node) => node.level > 0))
const parentMap = computed(() => groupBy(items.value, 'parentId'))

watchEffect(() => {
  if (props.expandAll) items.value = flatItems.value
  else items.value = tree.value
  if (sortBy.value.key) items.value = getSorted()
})
watch(flatItems, (v) => emit('get-nodes', v), { immediate: true })

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

function colHorizPaddingClass() {
  if (hasChild.value) return 'px-12'
  return 'px-6'
}

function levelPadding(node) {
  if (!hasChild.value) return '24px'
  const basePl = 8
  let levelPl = 30 * node.level
  if (node.leaf) levelPl += 40
  return `${basePl + levelPl}px`
}

function getSorted() {
  const firstGroupKey = Object.keys(parentMap.value)[0]
  return hierarchySort({
    groupItems: typy(parentMap.value[firstGroupKey]).safeArray,
    result: [],
  })
}

function hierarchySort({ groupItems, result }) {
  if (!groupItems.length) return result
  let items = groupItems.sort(compareFn)
  items.forEach((obj) => {
    result.push(obj)
    hierarchySort({ groupItems: typy(parentMap.value[obj.id]).safeArray, result })
  })
  return result
}

function getKeyTooltipData(key) {
  return {
    txt: key,
    collection: getKeyInfo(key),
    location: 'right',
    maxWidth: 300,
    whiteSpace: 'pre-wrap',
    transition: 'slide-x-transition',
    wordWrap: 'break-word',
  }
}

function getKeyInfo(key) {
  return props.keyInfoMap[key]
}

function hasKeyInfo(key) {
  return Boolean(getKeyInfo(key))
}
defineExpose({ headers })
</script>

<template>
  <VDataTable
    :headers="headers"
    :items="items"
    :items-per-page="-1"
    class="tree-table w-100"
    :class="{ 'tree-table--header-hidden': hideHeader }"
  >
    <template #headers="{ columns }">
      <tr>
        <template v-for="column in columns" :key="column.value">
          <CustomTblHeader
            :column="column"
            :sortBy="sortBy"
            :total="items.length"
            :showTotal="showKeyLength && column.value === 'key'"
            :class="`pa-0 ${colHorizPaddingClass()}`"
            @click="toggleSortBy(column.value)"
          />
        </template>
      </tr>
    </template>
    <template #[`item.key`]="{ item }">
      <GblTooltipActivator
        :data="getKeyTooltipData(item.key)"
        :activateOnTruncation="!hasKeyInfo(item.key)"
        tag="div"
        :style="{ paddingLeft: hasChild ? levelPadding(item) : 0 }"
        :class="[
          hasChild ? 'pr-12' : 'px-6',
          item.expanded ? 'font-weight-bold' : '',
          $typy(getKeyInfo(item.key), 'mandatory').safeBoolean ? 'label-required' : '',
          $typy(keyInfoMap).isEmptyObject ? '' : 'pointer',
        ]"
        :debounce="0"
        fillHeight
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
        <span v-mxs-highlighter="{ keyword: $attrs.search, txt: String(item.key) }">
          {{ item.key }}
        </span>
      </GblTooltipActivator>
    </template>
    <template #[`item.value`]="{ item }">
      <slot v-if="item.leaf" name="item.value" :item="item">
        <GblTooltipActivator
          activateOnTruncation
          :data="{
            txt: String(item.value),
            whiteSpace: 'pre-wrap',
            wordWrap: 'break-word',
          }"
          tag="div"
          fillHeight
          :class="`${colHorizPaddingClass()}`"
          :debounce="0"
          v-mxs-highlighter="{ keyword: $attrs.search, txt: String(item.value) }"
        />
      </slot>
    </template>
    <!-- Declare an empty bottom slot to hide the footer -->
    <template #bottom />
  </VDataTable>
</template>

<style lang="scss">
.tree-table {
  &--header-hidden {
    table {
      thead {
        display: none;
      }
      tbody {
        tr:first-child {
          td {
            border-top: vuetifyVar.$table-border;
          }
        }
      }
    }
  }
}
</style>
