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
import ResultDataTable from '@wkeComps/QueryEditor/ResultDataTable.vue'
import DurationTimer from '@wkeComps/QueryEditor/DurationTimer.vue'
import QueryResult from '@wsModels/QueryResult'
import { QUERY_MODES } from '@/constants/workspace'

const props = defineProps({
  dim: { type: Object, required: true },
  queryMode: { type: String, required: true },
  queryTabId: { type: String, required: true },
  queryTabTmp: { type: Object, required: true },
  isLoading: { type: Boolean, required: true },
  data: { type: Object, required: true },
  requestSentTime: { type: Number, required: true },
  execTime: { type: Number, required: true },
  totalDuration: { type: Number, required: true },
  resultDataTableProps: { type: Object, required: true },
})

const { PRVW_DATA, PRVW_DATA_DETAILS } = QUERY_MODES

const typy = useTypy()
const { t } = useI18n()
const TABS = [
  { id: PRVW_DATA, label: t('data') },
  { id: PRVW_DATA_DETAILS, label: t('details') },
]

const headerRef = ref(null)
const headerHeight = ref(0)

const resultData = computed(() => typy(props.data, 'data.attributes.results[0]').safeObjectOrEmpty)
const nodeQualifiedName = computed(
  () => typy(props.queryTabTmp, 'previewing_node.qualified_name').safeString
)

const activeMode = computed({
  get: () => props.queryMode,
  set(v) {
    if (isPrvwMode(props.queryMode))
      QueryResult.update({ where: props.queryTabId, data: { query_mode: v } })
  },
})

watch(
  () => props.isLoading,
  (v) => {
    if (!v) setHeaderHeight()
  }
)
watch(
  () => props.queryMode,
  async (v) => {
    if (!props.isLoading) await handleFetch(v)
  }
)

function setHeaderHeight() {
  if (headerRef.value) headerHeight.value = headerRef.value.clientHeight
}

function isPrvwMode(mode) {
  return mode === PRVW_DATA || mode === PRVW_DATA_DETAILS
}

async function handleFetch(mode) {
  if (isPrvwMode(mode) && !resultData.value.fields)
    await QueryResult.dispatch('fetchPrvw', {
      qualified_name: nodeQualifiedName.value,
      query_mode: mode,
    })
}
</script>

<template>
  <div class="fill-height">
    <div ref="headerRef" class="pb-2 result-header d-flex align-center">
      <template v-if="nodeQualifiedName">
        <div class="d-flex align-center mr-4">
          <b class="mr-1">Table:</b>
          <GblTooltipActivator
            :data="{ txt: nodeQualifiedName, offset: '0 16' }"
            activateOnTruncation
            :maxWidth="260"
          />
        </div>
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
        <VSpacer />
        <DurationTimer
          :startTime="requestSentTime"
          :executionTime="execTime"
          :totalDuration="totalDuration"
        />
      </template>
      <i18n-t v-else keypath="prvwTabGuide" tag="span" scope="global">
        <template #icon>
          <VIcon size="14" color="primary" icon="$mdiTableEye" />
        </template>
        <template #opt1>
          <b>{{ $t('previewData') }}</b>
        </template>
        <template #opt2>
          <b>{{ $t('viewDetails') }}</b>
        </template>
      </i18n-t>
    </div>
    <VSkeletonLoader
      v-if="isLoading"
      :loading="isLoading"
      type="table-tbody"
      :height="dim.height - headerHeight"
    />
    <template v-else>
      <KeepAlive>
        <ResultDataTable
          v-if="$typy(resultData, 'fields').safeArray.length"
          :key="queryMode"
          :height="dim.height - headerHeight"
          :width="dim.width"
          :headers="$typy(resultData, 'fields').safeArray.map((field) => ({ text: field }))"
          :data="$typy(resultData, 'data').safeArray"
          :metadata="$typy(resultData, 'metadata').safeArray"
          showGroupBy
          v-bind="resultDataTableProps"
        />
        <template v-else>
          <div v-for="(v, key) in resultData" :key="key">
            <b>{{ key }}:</b>
            <span class="d-inline-block ml-4">{{ v }}</span>
          </div>
        </template>
      </KeepAlive>
    </template>
  </div>
</template>
