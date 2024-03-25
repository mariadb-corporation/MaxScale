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
import DataPrvw from '@wkeComps/QueryEditor/DataPrvw.vue'
import ResultsTab from '@wkeComps/QueryEditor/ResultsTab.vue'
import HistoryAndSnippetsCtr from '@wkeComps/QueryEditor/HistoryAndSnippetsCtr.vue'
import { QUERY_MODES } from '@/constants/workspace'

const props = defineProps({
  dim: { type: Object, required: true },
  queryTab: { type: Object, required: true },
  queryTabConn: { type: Object, required: true },
  queryTabTmp: { type: Object, required: true },
  resultDataTableProps: { type: Object, required: true },
})

const typy = useTypy()
const { t } = useI18n()

const { QUERY_VIEW, PRVW_DATA, PRVW_DATA_DETAILS, HISTORY, SNIPPETS } = QUERY_MODES
const TABS = [
  { value: QUERY_VIEW, label: t('results') },
  { value: PRVW_DATA, label: t('dataPrvw') },
  { value: HISTORY, label: t('historyAndSnippets') },
]
const TAB_ITEM_CLASS = 'pt-2 px-5 mxs-field-text-size mxs-color-helper text-small-text'
const TAB_HEIGHT = 24
const queryTabId = computed(() => typy(props.queryTab, 'id').safeString)
const isConnBusy = computed(() => typy(props.queryTabConn, 'is_busy').safeBoolean)
const tabDim = computed(() => ({
  width: props.dim.width - 40, // px-5
  height: props.dim.height - TAB_HEIGHT - 8, // pt-2
}))
const queryMode = computed(() => typy(QueryResult.find(queryTabId.value), 'query_mode').safeString)
const activeTab = computed({
  get: () => {
    switch (queryMode.value) {
      case PRVW_DATA_DETAILS:
      case PRVW_DATA:
        return PRVW_DATA
      case SNIPPETS:
      case HISTORY:
        return HISTORY
      default:
        return queryMode.value
    }
  },
  set: (v) => QueryResult.update({ where: queryTabId.value, data: { query_mode: v } }),
})
const queryData = computed(() => {
  switch (queryMode.value) {
    case QUERY_VIEW:
      return typy(props.queryTabTmp, 'query_results').safeObjectOrEmpty
    case PRVW_DATA:
      return typy(props.queryTabTmp, 'prvw_data').safeObjectOrEmpty
    case PRVW_DATA_DETAILS:
      return typy(props.queryTabTmp, 'prvw_data_details').safeObjectOrEmpty
    default:
      return {}
  }
})
const isLoading = computed(() => typy(queryData.value, 'is_loading').safeBoolean)
const requestSentTime = computed(() => typy(queryData.value, 'request_sent_time').safeNumber)
const execTime = computed(() => {
  if (isLoading.value) return -1
  const execution_time = typy(queryData.value, 'data.attributes.execution_time').safeNumber
  if (execution_time) return parseFloat(execution_time.toFixed(4))
  return 0
})
const totalDuration = computed(() => typy(queryData.value, 'total_duration').safeNumber)

function getComponent(type) {
  let data = {
    component: '',
    props: {
      isLoading: isLoading.value,
      data: queryData.value,
      requestSentTime: requestSentTime.value,
      execTime: execTime.value,
      totalDuration: totalDuration.value,
    },
  }
  switch (type) {
    case QUERY_VIEW:
      data.component = ResultsTab
      break
    case PRVW_DATA:
    case PRVW_DATA_DETAILS:
      data.component = DataPrvw
      data.props = {
        ...data.props,
        queryMode: queryMode.value,
        queryTabId: queryTabId.value,
        queryTabTmp: props.queryTabTmp,
      }
      break
    default:
      data.component = HistoryAndSnippetsCtr
      data.props = {
        queryMode: queryMode.value,
        queryTabId: queryTabId.value,
      }
  }
  return data
}
</script>

<template>
  <div class="d-flex flex-column fill-height mxs-color-helper border-top-table-border">
    <VTabs v-model="activeTab" :height="TAB_HEIGHT">
      <VTab
        v-for="tab in TABS"
        :key="tab.value"
        :value="tab.value"
        :disabled="tab.value !== HISTORY && isConnBusy && isLoading"
        class="text-primary"
      >
        {{ tab.label }}
      </VTab>
    </VTabs>
    <VWindow v-model="activeTab" class="fill-height">
      <VWindowItem v-for="tab in TABS" :key="tab.value" :value="tab.value" class="fill-height">
        <KeepAlive>
          <component
            v-if="tab.value === activeTab"
            :is="getComponent(tab.value).component"
            v-bind="getComponent(tab.value).props"
            :dim="tabDim"
            :class="TAB_ITEM_CLASS"
            :resultDataTableProps="resultDataTableProps"
          />
        </KeepAlive>
      </VWindowItem>
    </VWindow>
  </div>
</template>
