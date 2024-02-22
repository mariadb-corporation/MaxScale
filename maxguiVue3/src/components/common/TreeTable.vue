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
  fixedLayout: { type: Boolean, default: false },
  keyWidth: { type: [String, Number], default: 'auto' },
  valueWidth: { type: [String, Number], default: 'auto' },
  keyInfoMap: { type: Object, default: () => ({}) },
  showKeyLength: { type: Boolean, default: false },
  arrayTransform: { type: Boolean, default: true },
  hideHeader: { type: Boolean, default: false },
})

const emit = defineEmits(['get-nodes'])

const headers = [
  { title: 'Variable', value: 'key', width: props.keyWidth },
  { title: 'Value', value: 'value', width: props.valueWidth },
]
const {
  lodash: { cloneDeep, groupBy },
} = useHelpers()
const typy = useTypy()

let items = ref([])
let sortBy = ref({ key: 'key', isDesc: false })

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

function sortOrder({ a, b, key, isDesc }) {
  if (isDesc) return b[key] < a[key] ? -1 : 1
  return a[key] < b[key] ? -1 : 1
}

function customSort(key) {
  sortBy.value.key = key
  sortBy.value.isDesc = !sortBy.value.isDesc

  const firstGroupKey = Object.keys(parentMap.value)[0]
  let result = hierarchySort({
    groupItems: typy(parentMap.value[firstGroupKey]).safeArray,
    key,
    isDesc: sortBy.value.isDesc,
    result: [],
  })
  items.value = result
}

function hierarchySort({ groupItems, key, isDesc, result }) {
  if (!groupItems.length) return result
  let items = groupItems.sort((a, b) => sortOrder({ a, b, key, isDesc }))
  items.forEach((obj) => {
    result.push(obj)
    hierarchySort({ groupItems: typy(parentMap.value[obj.id]).safeArray, key, isDesc, result })
  })
  return result
}

function getHeaderClass(columnKey) {
  let classes = [colHorizPaddingClass()]
  if (sortBy.value.key === columnKey) {
    classes.push('text-black table-header-sort--active')
    if (sortBy.value.isDesc) classes.push('table-header-sort--active-desc')
  }
  return classes
}

function getKeyTooltipData(key) {
  return {
    txt: key,
    collection: getKeyInfo(key),
    location: 'right',
    maxWidth: 300,
    whiteSpace: 'pre-wrap',
    transition: 'slide-x-transition',
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
    v-bind="$attrs"
    :headers="headers"
    :items="items"
    :items-per-page="-1"
    class="tree-table"
    :class="{ 'tree-table--fixed-layout': fixedLayout, 'tree-table--header-hidden': hideHeader }"
  >
    <template
      v-for="header in headers"
      :key="header.value"
      #[`header.${header.value}`]="{ column }"
    >
      <div
        class="fill-height d-inline-flex align-center pointer table-header rm-def-padding"
        :class="getHeaderClass(header.value)"
        @click="customSort(header.value)"
      >
        {{ column.title }}
        <template v-if="showKeyLength && header.value === 'key'">({{ items.length }})</template>
        <VIcon icon="mxs:arrowDown" size="14" class="ml-3 sort-icon" />
      </div>
    </template>
    <template #[`item.key`]="{ item }">
      <div
        class="d-flex align-stretch fill-height rm-def-padding"
        :class="[
          showCellBorder ? 'mxs-color-helper border-right-table-border' : '',
          item.expanded ? 'font-weight-bold' : '',
        ]"
      >
        <GblTooltipActivator
          :data="getKeyTooltipData(item.key)"
          :activateOnTruncation="!hasKeyInfo(item.key)"
          tag="div"
          class="cell-content w-100"
          :style="{ paddingLeft: hasChild ? levelPadding(item) : 0 }"
          :class="[hasChild ? 'pr-12' : 'px-6']"
          :debounce="0"
        >
          <VBtn
            v-if="$typy(item, 'children').safeArray.length"
            width="32"
            height="32"
            class="mr-1"
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
        </GblTooltipActivator>
      </div>
    </template>
    <template #[`item.value`]="{ item }">
      <div v-if="item.leaf" class="d-flex align-stretch fill-height rm-def-padding">
        <slot name="item.value" :item="item">
          <GblTooltipActivator
            activateOnTruncation
            :data="{ txt: String(item.value) }"
            tag="div"
            class="cell-content"
            :class="`${colHorizPaddingClass()}`"
            :debounce="0"
          />
        </slot>
      </div>
    </template>
    <!-- Declare an empty bottom slot to hide the footer -->
    <template #bottom />
  </VDataTable>
</template>

<style lang="scss" scoped>
.tree-table {
  width: 100%;
  // vuetifyVar.$table-column-padding left right is 24px
  .rm-def-padding {
    width: calc(100% + 48px);
    margin: 0px -24px;
  }
  .table-header {
    .sort-icon {
      visibility: hidden;
    }
    &-sort--active-desc {
      .sort-icon {
        transform: rotate(-180deg);
        transform-origin: center;
      }
    }
    &:hover,
    &-sort--active {
      .sort-icon {
        visibility: visible;
      }
    }
  }
  .cell-content {
    line-height: var(--v-table-row-height);
  }
}
</style>

<style lang="scss">
.tree-table {
  &--fixed-layout {
    table {
      table-layout: fixed;
    }
  }
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
