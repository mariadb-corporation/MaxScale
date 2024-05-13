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
import ResultExport from '@wkeComps/QueryEditor/ResultExport.vue'
import { NODE_CTX_TYPES } from '@/constants/workspace'

defineOptions({ inheritAttrs: false })
const props = defineProps({
  headers: {
    type: Array,
    validator: (arr) => {
      if (!arr.length) return true
      else return arr.filter((item) => 'text' in item).length === arr.length
    },
    required: true,
  },
  data: { type: Array, required: true },
  metadata: { type: Array, default: () => [] },
  height: { type: Number, required: true },
  width: { type: Number, required: true },
  showSelect: { type: Boolean, default: false },
  groupByColIdx: { type: Number, default: -1 },
  menuOpts: { type: Array, default: () => [] },
  isEditing: { type: Boolean, default: false },
  defExportFileName: { type: String, default: 'MaxScale Query Results' },
  hasInsertOpt: { type: Boolean, default: true },
  exportAsSQL: { type: Boolean, default: true },
  placeToEditor: { type: Function },
  onDragging: { type: Function },
  onDragend: { type: Function },
  onRowClick: { type: Function },
  defHiddenHeaderIndexes: { type: Array, default: () => [] },
  deleteItemBtnLabel: { type: String, default: 'delete' },
  deleteItemBtnTooltipTxt: { type: String, default: 'deleteSelectedRows' },
})
const emit = defineEmits(['get-headers', 'on-delete'])

const { CLIPBOARD, INSERT } = NODE_CTX_TYPES
const {
  lodash: { mergeWith, keyBy, cloneDeep },
  copyTextToClipboard,
  quotingIdentifier,
} = useHelpers()
const { t } = useI18n()
const typy = useTypy()

const tableToolsRef = ref(null)
const excludedSearchHeaderIndexes = ref([])
const hiddenHeaderIndexes = ref([])
const search = ref('')
const tableToolsHeight = ref(0)
const isVertTable = ref(false)
const activeGroupByColIndexes = ref([-1])
// states for ctx menu
const showCtxMenu = ref(false)
const ctxMenuData = ref({})

const columnsLimitInfo = ref('')

const activeGroupByColIdx = computed({
  get: () => activeGroupByColIndexes.value[0],
  set: (v) => (activeGroupByColIndexes.value = [v]),
})
const tableHeight = computed(() => props.height - tableToolsHeight.value - 8)
const draggable = computed(() => !props.isEditing)
const headersLength = computed(() => props.headers.length)
const tableHeaders = computed(() =>
  headersLength.value
    ? [
        {
          text: '#',
          maxWidth: 'max-content',
          hidden: hiddenHeaderIndexes.value.includes(0),
        },
        ...props.headers.map((h, i) => ({
          ...h,
          resizable: true,
          draggable: draggable.value,
          hidden: hiddenHeaderIndexes.value.includes(i + 1),
          useCellSlot: h.useCellSlot || (props.isEditing && h.editableCol),
        })),
      ]
    : []
)
const allHeaderNames = computed(() => tableHeaders.value.map((h) => h.text))
const visibleHeaders = computed(() => tableHeaders.value.filter((h) => !h.hidden))
const visHeaderNames = computed(() => visibleHeaders.value.map((h) => h.text))
const filterByColIndexes = computed(() =>
  allHeaderNames.value.reduce((acc, _, index) => {
    if (!excludedSearchHeaderIndexes.value.includes(index)) acc.push(index)
    return acc
  }, [])
)
const disableGrouping = computed(() => visHeaderNames.value.length <= 1 || isVertTable.value)
const isGrouping = computed(() => activeGroupByColIndexes.value[0] >= 0)
const tableData = computed(() => props.data.map((row, i) => [i + 1, ...row])) // add order number cell
const fields = computed(() => props.headers.map((h) => h.text))
const activeRow = computed(() => typy(ctxMenuData.value, 'row').safeArray)
const ctxMenuActivator = computed(() => `#${typy(ctxMenuData.value, 'activatorID').safeString}`)
const clipboardOpts = computed(() => genTxtOpts(CLIPBOARD))
const insertOpts = computed(() => genTxtOpts(INSERT))
const baseOpts = computed(() => {
  let opts = [{ title: t('copyToClipboard'), children: clipboardOpts.value }]
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

watch(showCtxMenu, (v) => {
  // when menu is closed by blur event, clear ctxMenuData so that activeRow can be reset
  if (!v) ctxMenuData.value = {}
})

watch(
  headersLength,
  (v) => {
    if (v > 50) {
      hiddenHeaderIndexes.value = Array.from(
        { length: tableHeaders.value.length - 50 },
        (_, index) => index + 50
      )
      columnsLimitInfo.value = t('info.columnsLimit')
    }
  },
  { immediate: true }
)
watch(
  tableHeaders,
  (v) => {
    emit('get-headers', v)
  },
  { deep: true, immediate: true }
)

onBeforeMount(() => (activeGroupByColIndexes.value = [props.groupByColIdx]))
onMounted(() => {
  nextTick(() => setTableToolsHeight())
  hiddenHeaderIndexes.value = cloneDeep(props.defHiddenHeaderIndexes)
})

function setTableToolsHeight() {
  if (tableToolsRef.value) tableToolsHeight.value = tableToolsRef.value.clientHeight
}

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
  <div class="result-data-table">
    <div ref="tableToolsRef" class="w-100 pb-2 d-inline-flex align-center">
      <slot name="left-table-tools-prepend" />
      <DebouncedTextField
        v-model="search"
        outlined
        density="compact"
        class="filter-result mr-2"
        :placeholder="$t('filterResult')"
        hide-details
      />
      <FilterList
        v-model="excludedSearchHeaderIndexes"
        reverse
        :label="$t('filterBy')"
        :items="visHeaderNames"
        :maxHeight="tableHeight - 20"
        returnIndex
        activatorClass="mr-2"
        :activatorProps="{ size: 'small', density: 'comfortable' }"
      />
      <FilterList
        v-model="activeGroupByColIndexes"
        :label="$t('groupBy')"
        :items="visHeaderNames"
        :maxHeight="tableHeight - 20"
        returnIndex
        hideSelectAll
        hideFilterIcon
        :multiple="false"
        :activatorProps="{ size: 'small', density: 'comfortable', disabled: disableGrouping }"
      />
      <slot name="left-table-tools-append" />
      <VSpacer />
      <VTooltip v-if="columnsLimitInfo" location="top" max-width="400">
        <template #activator="{ props }">
          <span class="text-truncate mx-2 d-flex align-center" v-bind="props">
            <VIcon size="16" color="warning" class="mr-2" icon="mxs:alertWarning" />
            {{ $t('columnsLimit') }}
          </span>
        </template>
        {{ columnsLimitInfo }}
      </VTooltip>
      <slot name="right-table-tools-prepend" />
      <TooltipBtn
        v-if="$typy($attrs, 'selectedItems').safeArray.length"
        class="mr-2 px-1 text-capitalize font-weight-medium"
        color="error"
        variant="outlined"
        density="comfortable"
        size="small"
        @click="emit('on-delete')"
      >
        <template #btn-content>
          {{ $t(deleteItemBtnLabel) }} ({{ $typy($attrs, 'selectedItems').safeArray.length }})
        </template>
        {{ $t(deleteItemBtnTooltipTxt) }}
      </TooltipBtn>
      <ResultExport
        :rows="data"
        :fields="fields"
        :defExportFileName="defExportFileName"
        :exportAsSQL="exportAsSQL"
        :metadata="metadata"
      />
      <FilterList
        v-model="hiddenHeaderIndexes"
        reverse
        :label="$t('columns')"
        :items="allHeaderNames"
        :maxHeight="tableHeight - 20"
        returnIndex
        :activatorProps="{ size: 'small', density: 'comfortable' }"
      />
      <TooltipBtn
        class="ml-2"
        size="small"
        :width="36"
        :min-width="'unset'"
        density="comfortable"
        color="primary"
        variant="outlined"
        :disabled="isGrouping"
        @click="isVertTable = !isVertTable"
      >
        <template #btn-content>
          <VIcon size="14" :class="{ 'rotate-left': !isVertTable }" icon="$mdiFormatRotate90" />
        </template>
        {{ $t(isVertTable ? 'switchToHorizTable' : 'switchToVertTable') }}
      </TooltipBtn>
      <slot name="right-table-tools-append" />
    </div>
    <VirtualScrollTbl
      class="pb-2"
      v-model:groupByColIdx="activeGroupByColIdx"
      :headers="tableHeaders"
      :data="tableData"
      :itemHeight="30"
      :maxHeight="tableHeight"
      :boundingWidth="width"
      :isVertTable="isVertTable"
      :showSelect="showSelect"
      :activeRow="activeRow"
      :search="search"
      :filterByColIndexes="filterByColIndexes"
      :contextmenuHandler="contextmenuHandler"
      v-bind="$attrs"
      @on-dragging="onDragging"
      @on-dragend="onDragend"
      @row-click="onRowClick"
    >
      <template v-for="(_, name) in $slots" #[name]="slotData">
        <slot :name="name" v-bind="slotData" />
      </template>
    </VirtualScrollTbl>
    <CtxMenu
      v-if="!$typy(ctxMenuData).isEmptyObject"
      :key="ctxMenuActivator"
      v-model="showCtxMenu"
      location="right"
      transition="slide-y-transition"
      :items="menuItems"
      :activator="ctxMenuActivator"
      @item-click="onChooseOpt"
    />
  </div>
</template>

<style lang="scss" scoped>
.filter-result {
  max-width: 250px;
}
</style>
