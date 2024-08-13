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
import RowLimit from '@wkeComps/QueryEditor/RowLimit.vue'
import OffsetInput from '@wkeComps/QueryEditor/OffsetInput.vue'
import ResultExport from '@wkeComps/QueryEditor/ResultExport.vue'
import { SNACKBAR_TYPE_MAP } from '@/constants'
import { NO_LIMIT } from '@/constants/workspace'
import { getStatementClasses, enforceLimitOffset, enforceNoLimit } from '@/utils/sqlLimiter'

const props = defineProps({
  height: { type: Number, default: 28 },
  showBtn: { type: Boolean, default: false },
  customFilterActive: { type: Boolean, default: false },
  selectedItems: { type: Array, default: () => [] },
  deleteItemBtnTooltipTxt: { type: String, default: 'deleteNRows' },
  tableHeight: { type: Number, default: 600 },
  allTableHeaderNames: { type: Array, default: () => [] },
  search: { type: String, default: '' },
  excludedSearchHeaderIndexes: { type: Array, default: () => [] },
  activeGroupByColIdx: { type: Number, default: -1 },
  hiddenHeaderIndexes: { type: Array, default: () => [] },
  isVertTable: { type: Boolean, default: false },
  defExportFileName: { type: String, default: 'MaxScale Query Results' },
  exportAsSQL: { type: Boolean, default: true },
  rows: { type: Array, default: () => [] },
  fields: { type: Array, default: () => [] },
  metadata: { type: Array, default: () => [] },
  statement: { type: Object, default: () => ({}) },
  onReload: { type: Function },
  onDelete: { type: Function },
})
const emit = defineEmits([
  'update:search',
  'update:excludedSearchHeaderIndexes',
  'update:activeGroupByColIdx',
  'update:hiddenHeaderIndexes',
  'update:isVertTable',
])

const ICON_BTN_DROPDOWN_HEIGHT = 300
const typy = useTypy()
const store = useStore()
const { t } = useI18n()
const {
  lodash: { cloneDeep },
} = useHelpers()

const isFilterMenuOpened = ref(false)
const validity = ref(null)
const rowLimit = ref(10000)
const offset = ref(0)

const query_row_limit = computed(() => store.state.prefAndStorage.query_row_limit)
const dropDownHeight = computed(() => Math.max(props.tableHeight - 20, 200))
const searchModel = computed({
  get: () => props.search,
  set: (v) => emit('update:search', v),
})
const excludedSearchHeaderIndexesModel = computed({
  get: () => props.excludedSearchHeaderIndexes,
  set: (v) => emit('update:excludedSearchHeaderIndexes', v),
})
const groupByColIdxModel = computed({
  get: () => [props.activeGroupByColIdx],
  set: (v) => emit('update:activeGroupByColIdx', v[0]),
})
const isVertTableModel = computed({
  get: () => props.isVertTable,
  set: (v) => emit('update:isVertTable', v),
})
const disableGrouping = computed(
  () => props.allTableHeaderNames.length <= 1 || isVertTableModel.value
)
const isGrouping = computed(() => groupByColIdxModel.value >= 0)
const hiddenHeaderIndexesModel = computed({
  get: () => props.hiddenHeaderIndexes,
  set: (v) => emit('update:hiddenHeaderIndexes', v),
})
const isFiltering = computed(() => Boolean(searchModel.value) || props.customFilterActive)
const isSelectStatement = computed(() => typy(props.statement, 'type').safeString === 'select')
const isNoLimit = computed(() => rowLimit.value === NO_LIMIT)
const stmtLimit = computed(() => props.statement.limit)
const stmtOffset = computed(() => typy(props.statement, 'offset').safeNumber)
const hasChangedOffset = computed(() => offset.value !== stmtOffset.value)
const hasChangedLimit = computed(() => rowLimit.value !== stmtLimit.value)

watch(
  () => props.statement,
  (v) => {
    const { limit } = v || {}
    if (typy(limit).isDefined) rowLimit.value = limit === 0 ? NO_LIMIT : limit
    else rowLimit.value = query_row_limit.value
    offset.value = stmtOffset.value
  },
  { deep: true, immediate: true }
)

function getStatementClass(sql) {
  const [, statementClasses] = getStatementClasses(sql)
  return typy(statementClasses, '[0]').safeObject
}

/**
 * Remove `LIMIT`
 * @param {class} statementClass
 * @returns {[string|null, object|null]}
 */
function setNoLimit(statementClass) {
  const [e, statement] = enforceNoLimit(statementClass)
  if (e) return [t('errors.enforceNoLimit'), null]
  return [null, statement]
}

/**
 * Insert or replace `LIMIT` and `OFFSET`
 * @param {class} statementClass
 * @returns {[string|null, object|null]}
 */
function setLimitAndOffset(statementClass) {
  const param = {
    statementClass,
    limit: rowLimit.value || query_row_limit.value, // fallback to default query_row_limit if value is empty
    mode: 'replace',
  }
  if (hasChangedOffset.value) param.offset = offset.value || 0 // fallback to default 0 if value is empty
  const [e, statement] = enforceLimitOffset(param)
  if (e) return [t('errors.injectLimit'), null]
  return [null, statement]
}

/**
 * @param {object} statement
 * @returns {[string|null, object|null]}
 */
function handleApplyLimitAndOffset(statement) {
  /**
   * Only select statement can be refreshed with new offset,limit injected.
   * Others will have their result set limited by the max_rows field in the api
   */
  if (isSelectStatement.value) {
    const statementClass = getStatementClass(statement.text)
    if (isNoLimit.value) return setNoLimit(statementClass)
    return setLimitAndOffset(statementClass)
  }
  return [null, { ...statement, limit: isNoLimit.value ? 0 : rowLimit.value }]
}

async function reload() {
  let newStatement = cloneDeep(props.statement),
    errMsg
  if (hasChangedLimit.value || hasChangedOffset.value) {
    ;[errMsg, newStatement] = handleApplyLimitAndOffset(newStatement)
  }
  if (errMsg)
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', { text: [errMsg], type: SNACKBAR_TYPE_MAP.ERROR })
  else await props.onReload(newStatement)
}
</script>

<template>
  <VSheet :height="height" class="w-100 d-inline-flex align-center">
    <slot name="toolbar-left-append" :showBtn="showBtn" />
    <VSpacer />
    <template v-if="showBtn">
      <VForm v-model="validity" class="d-flex align-center">
        <RowLimit
          v-if="!$typy(statement).isEmptyObject"
          v-model="rowLimit"
          :prefix="$t('limit')"
          :min-width="70"
          minimized
          hide-details
          borderless
          showErrInSnackbar
          hasNoLimit
          allowEmpty
          class="ml-1 flex-grow-0"
          :menu-props="{ 'max-height': dropDownHeight }"
        />
        <OffsetInput
          v-if="isSelectStatement && !isNoLimit"
          v-model="offset"
          class="ml-1 flex-grow-0"
        />
        <TooltipBtn
          v-if="$typy(onReload).isFunction"
          square
          variant="text"
          size="small"
          color="primary"
          class="ml-1"
          :disabled="!validity"
          data-test="reload-btn"
          @click="reload"
        >
          <template #btn-content>
            <VIcon size="14" icon="mxs:reload" />
          </template>
          {{ $t('reload') }}
        </TooltipBtn>
      </VForm>
    </template>
    <TooltipBtn
      v-if="showBtn && selectedItems.length && $typy(onDelete).isFunction"
      square
      variant="text"
      size="small"
      color="error"
      class="ml-1"
      data-test="delete-btn"
      @click="onDelete"
    >
      <template #btn-content>
        <VIcon size="14" icon="mxs:delete" />
      </template>
      {{ $t(deleteItemBtnTooltipTxt, { count: selectedItems.length }) }}
    </TooltipBtn>
    <slot name="toolbar-right-prepend" :showBtn="showBtn" />
    <template v-if="showBtn">
      <VMenu
        v-model="isFilterMenuOpened"
        transition="slide-y-transition"
        content-class="full-border"
        :close-on-content-click="false"
      >
        <template v-slot:activator="{ props }">
          <TooltipBtn
            square
            :variant="isFiltering ? 'flat' : 'text'"
            size="small"
            color="primary"
            class="ml-1"
            data-test="filter-btn"
            v-bind="props"
          >
            <template #btn-content>
              <VIcon size="14" icon="mxs:filter" />
            </template>
            {{ $t('filter') }}
          </TooltipBtn>
        </template>
        <VSheet :min-width="260" class="px-4 pt-4 pb-6 no-shadow">
          <h4>{{ $t('filter') }}</h4>
          <div class="py-2">
            <DebouncedTextField
              v-model="searchModel"
              outlined
              density="compact"
              hide-details
              class="my-2"
              data-test="filter-input"
            >
              <template #prepend-inner>
                <VIcon size="14" icon="mxs:search" />
              </template>
            </DebouncedTextField>
            <FilterList
              v-model="excludedSearchHeaderIndexesModel"
              reverse
              :label="$t('filterBy')"
              :items="allTableHeaderNames"
              :maxHeight="600"
              returnIndex
              :activatorProps="{ density: 'default', size: 'small', 'data-test': 'filter-by' }"
              activatorClass="mr-2"
            />
            <slot name="filter-menu-content-append" />
          </div>
        </VSheet>
      </VMenu>
      <FilterList
        v-model="groupByColIdxModel"
        :label="$t('groupBy')"
        :items="allTableHeaderNames"
        :maxHeight="ICON_BTN_DROPDOWN_HEIGHT"
        returnIndex
        hideSelectAll
        hideFilterIcon
        :multiple="false"
      >
        <template #activator="{ data: { props, label } }">
          <TooltipBtn
            square
            :variant="isGrouping ? 'flat' : 'text'"
            size="small"
            color="primary"
            :disabled="disableGrouping"
            class="ml-1"
            data-test="grouping-btn"
            v-bind="props"
          >
            <template #btn-content>
              <VIcon size="16" icon="$mdiFileTreeOutline" />
            </template>
            {{ label }}
          </TooltipBtn>
        </template>
      </FilterList>
      <FilterList
        v-model="hiddenHeaderIndexesModel"
        reverse
        hideFilterIcon
        :label="$t('columnVisibility')"
        :items="allTableHeaderNames"
        :maxHeight="ICON_BTN_DROPDOWN_HEIGHT"
        returnIndex
      >
        <template #activator="{ data: { props, label } }">
          <TooltipBtn
            square
            :variant="hiddenHeaderIndexesModel.length > 0 ? 'flat' : 'text'"
            size="small"
            color="primary"
            class="ml-1"
            data-test="col-vis-btn"
            v-bind="props"
          >
            <template #btn-content>
              <VIcon size="16" icon="$mdiEyeOutline" />
            </template>
            {{ label }}
          </TooltipBtn>
        </template>
      </FilterList>
      <TooltipBtn
        square
        :variant="isVertTableModel ? 'flat' : 'text'"
        size="small"
        color="primary"
        :disabled="isGrouping"
        class="ml-1"
        data-test="rotate-btn"
        @click="isVertTableModel = !isVertTableModel"
      >
        <template #btn-content>
          <VIcon
            size="16"
            :class="{ 'rotate-left': !isVertTableModel }"
            icon="$mdiFormatRotate90"
          />
        </template>
        {{ $t(isVertTableModel ? 'switchToHorizTable' : 'switchToVertTable') }}
      </TooltipBtn>
      <ResultExport
        :rows="rows"
        :fields="fields"
        :defExportFileName="defExportFileName"
        :exportAsSQL="exportAsSQL"
        :metadata="metadata"
        class="ml-1"
      />
    </template>
  </VSheet>
</template>
