<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import DataTableToolbar from '@wkeComps/QueryEditor/DataTableToolbar.vue'
import {
  NODE_CTX_TYPE_MAP,
  MAX_RENDERED_COLUMNS,
  COMPACT_TOOLBAR_HEIGHT,
} from '@/constants/workspace'

defineOptions({ inheritAttrs: false })

const props = defineProps({
  data: { type: Object, default: () => ({}) },
  height: { type: Number, required: true },
  width: { type: Number, required: true },
  customHeaders: { type: Array, default: () => [] },
  defHiddenHeaderIndexes: { type: Array, default: () => [] },
  draggableCell: { type: Boolean, default: true },
  groupByColIdx: { type: Number, default: -1 },
  menuOpts: { type: Array, default: () => [] },
  hasInsertOpt: { type: Boolean, default: true },
  placeToEditor: { type: Function },
  onDragging: { type: Function },
  onDragend: { type: Function },
  toolbarProps: { type: Object },
})

const emit = defineEmits(['get-table-headers'])

const { CLIPBOARD, INSERT } = NODE_CTX_TYPE_MAP
const typy = useTypy()

const {
  lodash: { mergeWith, keyBy, cloneDeep },
  copyTextToClipboard,
  quotingIdentifier,
} = useHelpers()
const { t } = useI18n()

const search = ref('')
const excludedSearchHeaderIndexes = ref([])
const activeGroupByColIdx = ref(props.groupByColIdx)
const hiddenHeaderIndexes = ref(cloneDeep(props.defHiddenHeaderIndexes))
const isVertTable = ref(false)

const showCtxMenu = ref(false)
const ctxMenuData = ref({})

const tableHeight = computed(() => props.height - COMPACT_TOOLBAR_HEIGHT)
const headers = computed(() =>
  props.customHeaders.length
    ? props.customHeaders
    : typy(props.data, 'fields').safeArray.map((field) => ({ text: field }))
)
const fields = computed(() => headers.value.map((h) => h.text))
const headersLength = computed(() => headers.value.length)
const tableHeaders = computed(() => {
  return [
    {
      text: '#',
      maxWidth: 'max-content',
      hidden: hiddenHeaderIndexes.value.includes(0),
    },
    ...headers.value.map((h, i) => ({
      ...h,
      resizable: true,
      draggable: props.draggableCell,
      hidden: hiddenHeaderIndexes.value.includes(i + 1),
      useCellSlot: h.useCellSlot,
    })),
  ]
})
const allTableHeaderNames = computed(() => tableHeaders.value.map((h) => h.text))
const tableRows = computed(
  () => typy(props.data, 'data').safeArray.map((row, i) => [i + 1, ...row]) // add order number cell
)
const metadata = computed(() => typy(props.data, 'metadata').safeArray)
const filterByColIndexes = computed(() =>
  allTableHeaderNames.value.reduce((acc, _, index) => {
    if (!excludedSearchHeaderIndexes.value.includes(index)) acc.push(index)
    return acc
  }, [])
)
const hasData = computed(
  () => typy(props.data, 'fields').isDefined || typy(props.data, 'data').isDefined
)

const activeRow = computed(() => typy(ctxMenuData.value, 'row').safeArray)
const ctxMenuActivator = computed(() => `#${typy(ctxMenuData.value, 'activatorID').safeString}`)
const clipboardOpts = computed(() => genTxtOpts(CLIPBOARD))
const insertOpts = computed(() => genTxtOpts(INSERT))
const baseOpts = computed(() => {
  const opts = [{ title: t('copyToClipboard'), children: clipboardOpts.value }]
  if (props.hasInsertOpt) {
    opts.unshift({ title: t('placeToEditor'), children: insertOpts.value })
  }
  return opts
})
const menuItems = computed(() => {
  if (props.menuOpts.length) {
    // Deep merge of menuOpts with baseOpts
    const merged = Object.values(
      mergeWith(
        keyBy(baseOpts.value, 'title'),
        keyBy(props.menuOpts, 'title'),
        (objVal, srcVal) => {
          if (Array.isArray(objVal)) return objVal.concat(srcVal)
        }
      )
    )
    return merged
  }
  return baseOpts.value
})

watch(
  tableHeaders,
  (v) => {
    emit('get-table-headers', v)
  },
  { deep: true, immediate: true }
)
watch(
  headersLength,
  (v) => {
    if (v > MAX_RENDERED_COLUMNS) {
      hiddenHeaderIndexes.value = Array.from(
        { length: tableHeaders.value.length - MAX_RENDERED_COLUMNS },
        (_, index) => index + MAX_RENDERED_COLUMNS
      )
    }
  },
  { immediate: true }
)
watch(showCtxMenu, (v) => {
  // when menu is closed by blur event, clear ctxMenuData so that activeRow can be reset
  if (!v) ctxMenuData.value = {}
})

function contextmenuHandler(data) {
  const { activatorID } = data
  if (typy(ctxMenuData.value, 'activatorID').safeString === activatorID) {
    showCtxMenu.value = false
    ctxMenuData.value = {}
  } else {
    showCtxMenu.value = true
    ctxMenuData.value = data
  }
}

/**
 * Both INSERT and CLIPBOARD types have same options & action
 * This generates txt options based on provided type
 * @param {String} type - INSERT OR CLIPBOARD
 * @returns {Array} - return context options
 */
function genTxtOpts(type) {
  return [t('fieldQuoted'), t('field')].map((title) => ({
    title,
    action: ({ opt, data }) => handleTxtOpt({ opt, data }),
    type,
  }))
}

// Handle edge case when cell value is an object. e.g. In History table
function processField(cell) {
  // convert to string with template literals
  return typy(cell).isObject ? `${cell.name}` : `${cell}`
}

/**
 * Both INSERT and CLIPBOARD types have same options and action
 * This handles INSERT and CLIPBOARD options
 * @param {data} item - data
 * @param {Object} opt - context menu option
 */
function handleTxtOpt({ opt, data }) {
  let v = ''
  switch (opt.title) {
    case t('fieldQuoted'):
      v = quotingIdentifier(processField(data.cell))
      break
    case t('field'):
      v = processField(data.cell)
      break
  }
  switch (opt.type) {
    case INSERT:
      typy(props, 'placeToEditor').safeFunction(v)
      break
    case CLIPBOARD:
      copyTextToClipboard(v)
      break
  }
}

function onChooseOpt(opt) {
  // pass arguments opt and data to action function
  opt.action({ opt, data: ctxMenuData.value })
}
</script>

<template>
  <DataTableToolbar
    v-model:search="search"
    v-model:excludedSearchHeaderIndexes="excludedSearchHeaderIndexes"
    v-model:activeGroupByColIdx="activeGroupByColIdx"
    v-model:hiddenHeaderIndexes="hiddenHeaderIndexes"
    v-model:isVertTable="isVertTable"
    :height="COMPACT_TOOLBAR_HEIGHT"
    :showBtn="hasData"
    :selectedItems="$typy($attrs, 'selectedItems').safeArray"
    :tableHeight="tableHeight"
    :allTableHeaderNames="allTableHeaderNames"
    :rows="typy(data, 'data').safeArray"
    :fields="fields"
    :metadata="metadata"
    v-bind="toolbarProps"
  >
    <template v-for="(_, name) in $slots" #[name]="slotData">
      <slot :name="name" v-bind="slotData" />
    </template>
  </DataTableToolbar>
  <CtxMenu
    v-if="!$typy(ctxMenuData).isEmptyObject && hasData"
    :key="ctxMenuActivator"
    v-model="showCtxMenu"
    location="right"
    transition="slide-y-transition"
    :items="menuItems"
    :activator="ctxMenuActivator"
    @item-click="onChooseOpt"
  />
  <VirtualScrollTbl
    v-if="hasData"
    v-model:groupByColIdx="activeGroupByColIdx"
    :itemHeight="30"
    :maxHeight="tableHeight"
    :boundingWidth="width"
    :headers="tableHeaders"
    :data="tableRows"
    :search="search"
    :filterByColIndexes="filterByColIndexes"
    :isVertTable="isVertTable"
    :activeRow="activeRow"
    :contextmenuHandler="contextmenuHandler"
    v-bind="$attrs"
    @on-dragging="typy(onDragging).safeFunction"
    @on-dragend="typy(onDragend).safeFunction"
  >
    <template v-for="(_, name) in $slots" #[name]="slotData">
      <slot :name="name" v-bind="slotData" />
    </template>
  </VirtualScrollTbl>
  <div v-else :style="{ height: `${tableHeight}px` }" v-bind="$attrs">
    <div v-for="(v, key) in data" :key="key">
      <template v-if="key !== 'statement'">
        <b>{{ key }}:</b>
        <span class="d-inline-block ml-4">{{ v }}</span>
      </template>
    </div>
    <slot name="result-msg-append" />
  </div>
</template>
