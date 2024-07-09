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
import QueryResult from '@wsModels/QueryResult'
import DataPreviewer from '@wkeComps/QueryEditor/DataPreviewer.vue'
import ResultsTab from '@wkeComps/QueryEditor/ResultsTab.vue'
import HistoryAndSnippetsCtr from '@wkeComps/QueryEditor/HistoryAndSnippetsCtr.vue'
import ProcessListCtr from '@wkeComps/QueryEditor/ProcessListCtr.vue'
import workspace from '@/composables/workspace'
import { QUERY_MODES } from '@/constants/workspace'

const props = defineProps({
  dim: { type: Object, required: true },
  queryTab: { type: Object, required: true },
  queryTabConn: { type: Object, required: true },
  queryTabTmp: { type: Object, required: true },
  dataTableProps: { type: Object, required: true },
})

const typy = useTypy()
const { t } = useI18n()

const { QUERY_VIEW, PRVW_DATA, PRVW_DATA_DETAILS, HISTORY, SNIPPETS, PROCESSLIST } = QUERY_MODES
const TABS = [
  { value: QUERY_VIEW, label: t('results') },
  { value: PRVW_DATA, label: t('dataPrvw') },
  { value: PROCESSLIST, label: t('processlist') },
  { value: HISTORY, label: t('historyAndSnippets') },
]
const TAB_ITEM_CLASS = 'text-body-2 text-small-text'
const TAB_HEIGHT = 24

const queryTabId = computed(() => typy(props.queryTab, 'id').safeString)
const isConnBusy = computed(() => typy(props.queryTabConn, 'is_busy').safeBoolean)
const tabDim = computed(() => ({
  width: props.dim.width,
  height: props.dim.height - TAB_HEIGHT,
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
const queryResults = computed(() => typy(props.queryTabTmp, 'query_results').safeObjectOrEmpty)
const prvwData = computed(() => typy(props.queryTabTmp, 'prvw_data').safeObjectOrEmpty)
const prvwDataDetails = computed(
  () => typy(props.queryTabTmp, 'prvw_data_details').safeObjectOrEmpty
)
const processList = computed(() => typy(props.queryTabTmp, 'process_list').safeObjectOrEmpty)
const queryData = computed(() => {
  switch (queryMode.value) {
    case QUERY_VIEW:
      return queryResults.value
    case PRVW_DATA:
      return prvwData.value
    case PRVW_DATA_DETAILS:
      return prvwDataDetails.value
    case PROCESSLIST:
      return processList.value
    default:
      return {}
  }
})
const { isLoading } = workspace.useCommonResSetAttrs(queryData)

function getComponent() {
  let data = { component: '' }

  switch (activeTab.value) {
    case QUERY_VIEW:
      data.component = ResultsTab
      data.props = { data: queryResults.value }
      break
    case PRVW_DATA:
    case PRVW_DATA_DETAILS:
      data.component = DataPreviewer
      data.props = {
        prvwData: prvwData.value,
        prvwDataDetails: prvwDataDetails.value,
        queryMode: queryMode.value,
        queryTabId: queryTabId.value,
        queryTabTmp: props.queryTabTmp,
      }
      break
    case HISTORY:
    case SNIPPETS:
      data.component = HistoryAndSnippetsCtr
      data.props = {
        queryMode: queryMode.value,
        queryTabId: queryTabId.value,
      }
      break
    case PROCESSLIST:
      data.component = ProcessListCtr
      data.props = {
        data: processList.value,
        queryTabConn: props.queryTabConn,
      }
  }
  return data
}
</script>

<template>
  <div class="fill-height border-top--table-border">
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
    <VSlideXTransition>
      <KeepAlive>
        <component
          :is="getComponent().component"
          v-bind="getComponent().props"
          :dim="tabDim"
          :class="TAB_ITEM_CLASS"
          :style="{ height: `${tabDim.height}px` }"
          :dataTableProps="dataTableProps"
        />
      </KeepAlive>
    </VSlideXTransition>
  </div>
</template>
