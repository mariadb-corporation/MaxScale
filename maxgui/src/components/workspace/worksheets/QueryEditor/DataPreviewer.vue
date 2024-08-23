<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import QueryResult from '@wsModels/QueryResult'
import ResultView from '@/components/workspace/worksheets/QueryEditor/ResultView.vue'
import queryResultService from '@wsServices/queryResultService'
import { QUERY_MODE_MAP } from '@/constants/workspace'

const props = defineProps({
  dim: { type: Object, required: true },
  queryMode: { type: String, required: true },
  queryTabId: { type: String, required: true },
  queryTabTmp: { type: Object, required: true },
  prvwData: { type: Object, required: true },
  prvwDataDetails: { type: Object, required: true },
  dataTableProps: { type: Object, required: true },
})

const { PRVW_DATA, PRVW_DATA_DETAILS } = QUERY_MODE_MAP

const typy = useTypy()
const { t } = useI18n()

const TABS = [
  { id: PRVW_DATA, label: t('data') },
  { id: PRVW_DATA_DETAILS, label: t('details') },
]

const prvwDataRes = computed(() => typy(props.prvwData, 'data.attributes.results[0]').safeObject)
const detailsDataRes = computed(
  () => typy(props.prvwDataDetails, 'data.attributes.results[0]').safeObject
)

const queryDataMap = computed(() => ({
  PRVW_DATA: props.prvwData,
  PRVW_DATA_DETAILS: props.prvwDataDetails,
}))

const nodeQualifiedName = computed(
  () => typy(props.queryTabTmp, 'active_node.qualified_name').safeString
)

async function fetch() {
  await queryResultService.queryPrvw({
    qualified_name: nodeQualifiedName.value,
    query_mode: props.queryMode,
  })
}
async function changeMode(v) {
  await QueryResult.update({ where: props.queryTabId, data: { query_mode: v } }).then(async () => {
    if (v === PRVW_DATA ? !prvwDataRes.value : !detailsDataRes.value) await fetch()
  })
}
async function reload(statement) {
  await queryResultService.queryPrvw({ customStatement: statement, query_mode: props.queryMode })
}
</script>

<template>
  <div class="data-previewer">
    <i18n-t
      v-if="!nodeQualifiedName"
      keypath="prvwTabGuide"
      scope="global"
      tag="div"
      class="pt-2 px-5"
    >
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
    <KeepAlive v-for="(data, mode) in queryDataMap" :key="mode">
      <ResultView
        v-if="queryMode === mode"
        :data="data"
        :dataTableProps="dataTableProps"
        :dim="dim"
        :reload="reload"
        class="fill-height"
      >
        <template v-if="nodeQualifiedName" #toolbar-left-append>
          <VTabs
            :model-value="queryMode"
            hide-slider
            :height="20"
            class="workspace-tab-style"
            @update:model-value="changeMode"
          >
            <VTab
              v-for="tab in TABS"
              :key="tab.id"
              :value="tab.id"
              class="px-3 text-uppercase border--table-border"
              selectedClass="v-tab--selected font-weight-medium"
            >
              {{ tab.label }}
            </VTab>
          </VTabs>
        </template>
      </ResultView>
    </KeepAlive>
  </div>
</template>
