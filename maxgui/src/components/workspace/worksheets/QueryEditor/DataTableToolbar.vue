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
import ResultExport from '@wkeComps/QueryEditor/ResultExport.vue'

const props = defineProps({
  height: { type: Number, default: 28 },
  showBtn: { type: Boolean, default: false },
  columnsLimitInfo: { type: String, default: '' },
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
})
const emit = defineEmits([
  'update:search',
  'update:excludedSearchHeaderIndexes',
  'update:activeGroupByColIdx',
  'update:hiddenHeaderIndexes',
  'update:isVertTable',
  'on-delete',
])

const isFilterMenuOpened = ref(false)

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
</script>

<template>
  <VSheet :height="height" class="w-100 d-inline-flex align-center">
    <slot name="toolbar-left-append" :showBtn="showBtn" />
    <VSpacer />
    <!-- TODO: Move this to InfoPane -->
    <template v-if="showBtn">
      <VTooltip v-if="columnsLimitInfo" location="top" max-width="400">
        <template #activator="{ props }">
          <span class="text-truncate mx-2 d-flex align-center" v-bind="props">
            <VIcon size="14" color="warning" class="mr-2" icon="mxs:alertWarning" />
            {{ $t('columnsLimit') }}
          </span>
        </template>
        {{ columnsLimitInfo }}
      </VTooltip>
      <TooltipBtn
        v-if="selectedItems.length"
        square
        variant="text"
        size="small"
        color="error"
        @click="emit('on-delete')"
      >
        <template #btn-content>
          <VIcon size="14" icon="mxs:delete" />
        </template>
        {{ $t(deleteItemBtnTooltipTxt, { count: selectedItems.length }) }}
      </TooltipBtn>
    </template>
    <slot name="toolbar-right-prepend" :showBtn="showBtn" />
    <template v-if="showBtn">
      <VMenu
        v-model="isFilterMenuOpened"
        transition="slide-y-transition"
        content-class="full-border"
        :close-on-content-click="false"
      >
        <template v-slot:activator="{ props }">
          <TooltipBtn square variant="text" size="small" color="primary" v-bind="props">
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
              :activatorProps="{ density: 'default', size: 'small' }"
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
        :maxHeight="tableHeight - 20"
        returnIndex
        hideSelectAll
        hideFilterIcon
        :multiple="false"
      >
        <template #activator="{ data: { props, label } }">
          <TooltipBtn
            square
            variant="text"
            size="small"
            color="primary"
            :disabled="disableGrouping"
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
        :maxHeight="tableHeight - 20"
        returnIndex
      >
        <template #activator="{ data: { props, label } }">
          <TooltipBtn square variant="text" size="small" color="primary" v-bind="props">
            <template #btn-content>
              <VIcon size="16" icon="$mdiEyeOutline" />
            </template>
            {{ label }}
          </TooltipBtn>
        </template>
      </FilterList>
      <TooltipBtn
        square
        variant="text"
        size="small"
        color="primary"
        :disabled="isGrouping"
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
      />
    </template>
  </VSheet>
</template>
