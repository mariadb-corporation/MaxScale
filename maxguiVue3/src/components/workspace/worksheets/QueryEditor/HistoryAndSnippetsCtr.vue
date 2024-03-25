<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import QueryResult from '@wsModels/QueryResult'
import ResultDataTable from '@wkeComps/QueryEditor/ResultDataTable.vue'
import { QUERY_MODES, NODE_CTX_TYPES, QUERY_LOG_TYPES, OS_KEY } from '@/constants/workspace'

const props = defineProps({
  dim: { type: Object, required: true },
  queryMode: { type: String, required: true },
  queryTabId: { type: String, required: true },
  resultDataTableAttrs: { type: Object, default: () => ({}) },
  resultDataTableProps: { type: Object, required: true },
})

const { HISTORY, SNIPPETS } = QUERY_MODES
const { CLIPBOARD, INSERT } = NODE_CTX_TYPES
const store = useStore()
const { t } = useI18n()
const typy = useTypy()
const {
  map2dArr,
  copyTextToClipboard,
  dateFormat,
  lodash: { cloneDeep, xorWith, isEqual },
} = useHelpers()

const LOG_TYPES = Object.values(QUERY_LOG_TYPES)
const DATE_FORMAT_TYPE = 'E, dd MMM yyyy'
const TABS = [
  { id: HISTORY, label: t('history') },
  { id: SNIPPETS, label: t('snippets') },
]

const headerRef = ref(null)
const headerHeight = ref(0)
const itemsToBeDeleted = ref([])
const logTypesToShow = ref([])
const isConfDlgOpened = ref(false)
const actionCellData = ref(null)

const query_history = computed(() => store.state.prefAndStorage.query_history)
const query_snippets = computed(() => store.state.prefAndStorage.query_snippets)
const activeMode = computed({
  get: () => props.queryMode,
  set(v) {
    if (props.queryMode === HISTORY || props.queryMode === SNIPPETS)
      QueryResult.update({ where: props.queryTabId, data: { query_mode: v } })
  },
})
const headers = computed(() => {
  let data = []
  switch (activeMode.value) {
    case HISTORY:
      data = query_history.value
      break
    case SNIPPETS:
      data = query_snippets.value
  }
  return Object.keys(typy(data[0]).safeObjectOrEmpty).map((field) => {
    let header = {
      text: field,
      capitalize: true,
    }
    // assign default width to each column to have better view
    switch (field) {
      case 'date':
        header.width = 150
        header.useCellSlot = true
        header.dateFormatType = DATE_FORMAT_TYPE
        break
      case 'connection_name':
        header.width = 215
        break
      case 'time':
        header.width = 90
        break
      case 'action':
        header.useCellSlot = true
        header.valuePath = 'name'
        break
      // Fields for QUERY_MODES.SNIPPETS
      case 'name':
        header.width = 240
        header.editableCol = true
        break
      case 'sql':
        header.editableCol = true
    }
    return header
  })
})
// result-data-table auto adds an order number header, so plus 1
const idxOfDateCol = computed(() => headers.value.findIndex((h) => h.text === 'date') + 1)
const persistedQueryData = computed(() => {
  switch (activeMode.value) {
    case HISTORY:
      return query_history.value
    case SNIPPETS:
      return query_snippets.value
    default:
      return []
  }
})
const rows = computed(() => persistedQueryData.value.map((item) => Object.values(item)))
const currRows = computed(() => {
  let data = persistedQueryData.value
  if (
    activeMode.value === HISTORY &&
    logTypesToShow.value.length &&
    logTypesToShow.value.length < LOG_TYPES.length
  )
    data = data.filter((log) => logTypesToShow.value.includes(log.action.type))
  return data.map((item) => Object.values(item))
})
const menuOpts = computed(() => {
  return [
    {
      title: t('copyToClipboard'),
      children: [
        {
          title: 'SQL',
          type: CLIPBOARD,
          action: ({ opt, data }) => txtOptHandler({ opt, data }),
        },
      ],
    },
    {
      title: t('placeToEditor'),
      children: [
        {
          title: 'SQL',
          type: INSERT,
          action: ({ opt, data }) => txtOptHandler({ opt, data }),
        },
      ],
    },
  ]
})

onMounted(() => setHeaderHeight())

function setHeaderHeight() {
  if (headerRef.value) headerHeight.value = headerRef.value.clientHeight
}

function handleDeleteSelectedRows(rows) {
  itemsToBeDeleted.value = rows
  isConfDlgOpened.value = true
}

function deleteSelectedRows() {
  let targetMatrices = cloneDeep(itemsToBeDeleted.value).map(
    (row) => row.filter((_, i) => i !== 0) // Remove # col
  )
  const newMaxtrices = xorWith(rows.value, targetMatrices, isEqual)
  // Convert to array of objects
  const newData = map2dArr({
    fields: headers.value.map((h) => h.text),
    arr: newMaxtrices,
  })
  store.commit(`prefAndStorage/SET_QUERY_${activeMode.value}`, newData)
}

function txtOptHandler({ opt, data }) {
  let rowData = map2dArr({
    fields: headers.value.map((h) => h.text),
    arr: [data.row.filter((_, i) => i !== 0)], // Remove # col
  })
  let sql, name
  switch (activeMode.value) {
    case HISTORY: {
      name = rowData[0].action.name
      sql = rowData[0].action.sql
      break
    }
    case SNIPPETS:
      sql = rowData[0].sql
  }
  const { INSERT, CLIPBOARD } = NODE_CTX_TYPES
  // if no name is defined when storing the query, sql query is stored to name
  let sqlTxt = sql ? sql : name
  switch (opt.type) {
    case CLIPBOARD:
      copyTextToClipboard(sqlTxt)
      break
    case INSERT:
      typy(props.resultDataTableProps, 'placeToEditor').safeFunction(sqlTxt)
      break
  }
}

function onDoneEditingSnippets(changedCells) {
  let cells = cloneDeep(changedCells)
  let snippets = cloneDeep(query_snippets.value)
  cells.forEach((c) => {
    delete c.objRow['#'] // Remove # col
    const idxOfRow = query_snippets.value.findIndex((item) => isEqual(item, c.objRow))
    if (idxOfRow > -1) snippets[idxOfRow] = { ...snippets[idxOfRow], [c.colName]: c.value }
  })

  store.commit('prefAndStorage/SET_QUERY_SNIPPETS', snippets)
}

function formatDate(cell) {
  return dateFormat({
    value: cell,
    formatType: DATE_FORMAT_TYPE,
  })
}
</script>

<template>
  <div class="fill-height">
    <div ref="headerRef" class="pb-2 result-header d-flex align-center">
      <VTabs v-model="activeMode" hide-slider :height="20" class="workspace-tab-style">
        <VTab
          v-for="tab in TABS"
          :key="tab.id"
          :value="tab.id"
          class="px-3 text-uppercase mxs-color-helper all-border-table-border"
          selectedClass="v-tab--selected font-weight-medium"
        >
          {{ tab.label }}
        </VTab>
      </VTabs>
    </div>
    <KeepAlive>
      <template v-if="persistedQueryData.length">
        <ResultDataTable
          v-if="activeMode === QUERY_MODES.HISTORY || activeMode === QUERY_MODES.SNIPPETS"
          :key="activeMode"
          :height="dim.height - headerHeight"
          :width="dim.width"
          :headers="headers"
          :data="currRows"
          showSelect
          showGroupBy
          :groupByColIdx="idxOfDateCol"
          :menuOpts="menuOpts"
          :showEditBtn="activeMode === QUERY_MODES.SNIPPETS"
          :defExportFileName="`MaxScale Query ${
            activeMode === QUERY_MODES.HISTORY ? 'History' : 'Snippets'
          }`"
          :exportAsSQL="false"
          @on-delete-selected="handleDeleteSelectedRows"
          @on-done-editing="onDoneEditingSnippets"
          v-bind="resultDataTableProps"
        >
          <template #header-connection_name="{ data: { maxWidth, activatorID } }">
            <GblTooltipActivator
              :data="{ txt: 'Connection Name', activatorID }"
              activateOnTruncation
              :maxWidth="maxWidth"
            />
          </template>
          <template v-if="activeMode === QUERY_MODES.SNIPPETS" #header-name>
            {{ $t('prefix') }}
          </template>
          <template #date="{ props, highlighterData, data: { cell } }">
            <span
              v-mxs-highlighter="{ ...highlighterData, txt: formatDate(cell) }"
              class="text-truncate"
              v-bind="props"
            >
              {{ formatDate(cell) }}
            </span>
          </template>
          <template #action="{ on, highlighterData, data: { cell, activatorID } }">
            <div
              v-mxs-highlighter="{ ...highlighterData, txt: cell.name }"
              class="text-truncate"
              @mouseenter="actionCellData = { data: cell, activatorID }"
              @mouseleave="actionCellData = null"
              v-on="on"
            >
              {{ cell.name }}
            </div>
          </template>
          <template v-if="activeMode === QUERY_MODES.HISTORY" #left-table-tools-append>
            <FilterList
              v-model="logTypesToShow"
              :label="$t('logTypes')"
              :items="LOG_TYPES"
              :maxHeight="200"
              hideSelectAll
              hideSearch
              :activatorProps="{ size: 'small', density: 'comfortable' }"
              activatorClass="ml-2"
            />
          </template>
        </ResultDataTable>
      </template>
      <i18n-t
        v-else
        :keypath="activeMode === QUERY_MODES.HISTORY ? 'historyTabGuide' : 'snippetTabGuide'"
        class="d-flex align-center"
        tag="span"
        scope="global"
      >
        <template #shortcut>
          &nbsp;<b>{{ OS_KEY }} + S</b>&nbsp;
        </template>
        <template #icon>
          &nbsp;
          <VIcon color="primary" size="16" icon="$mdiStarPlusOutline" />
          &nbsp;
        </template>
      </i18n-t>
    </KeepAlive>
    <BaseDlg
      v-model="isConfDlgOpened"
      :title="
        activeMode === QUERY_MODES.HISTORY
          ? $t('clearSelectedQueries', {
              targetType: $t('queryHistory'),
            })
          : $t('deleteSnippets')
      "
      saveText="delete"
      minBodyWidth="624px"
      :onSave="deleteSelectedRows"
    >
      <template #form-body>
        <p class="mb-4">
          {{
            $t('info.clearSelectedQueries', {
              quantity: itemsToBeDeleted.length === rows.length ? $t('entire') : $t('selected'),
              targetType: $t(activeMode === QUERY_MODES.HISTORY ? 'queryHistory' : 'snippets'),
            })
          }}
        </p>
      </template>
    </BaseDlg>
    <VTooltip
      v-if="$typy(actionCellData, 'activatorID').safeString"
      :modelValue="Boolean(actionCellData)"
      top
      transition="slide-y-transition"
      :activator="`#${actionCellData.activatorID}`"
    >
      <table class="action-table-tooltip px-1">
        <caption class="text-left font-weight-bold mb-3 pl-1">
          {{
            $t('queryResInfo')
          }}
          <VDivider class="mxs-color-helper border-separator" />
        </caption>
        <tr v-for="(value, key) in actionCellData.data" :key="`${key}`">
          <template v-if="key !== 'type'">
            <td>{{ key }}</td>
            <td
              :class="{ 'text-truncate': key !== 'response' }"
              :style="{ maxWidth: '600px', whiteSpace: key !== 'response' ? 'nowrap' : 'pre-line' }"
            >
              {{ value }}
            </td>
          </template>
        </tr>
      </table>
    </VTooltip>
  </div>
</template>

<style lang="scss" scoped>
.action-table-tooltip {
  border-spacing: 0;
  td {
    font-size: 0.875rem;
    height: 24px;
    vertical-align: middle;
    &:first-of-type {
      padding-right: 16px;
    }
  }
}
</style>
