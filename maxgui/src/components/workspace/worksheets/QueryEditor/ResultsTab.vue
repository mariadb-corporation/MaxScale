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
import QueryResultTabWrapper from '@/components/workspace/worksheets/QueryEditor/QueryResultTabWrapper.vue'
import ResultSetItems from '@wkeComps/QueryEditor/ResultSetItems.vue'
import DataTable from '@/components/workspace/worksheets/QueryEditor/DataTable.vue'
import workspace from '@/composables/workspace'
import { OS_KEY } from '@/constants/workspace'

const props = defineProps({
  dim: { type: Object, required: true },
  data: { type: Object, required: true },
  dataTableProps: { type: Object, required: true },
})

const typy = useTypy()
const ERR_TAB_ID = 'Error'

const activeResultsetId = ref('')

const queryData = computed(() => props.data)
const { isLoading, requestSentTime, execTime, totalDuration } =
  workspace.useCommonResSetAttrs(queryData)

const hasRes = computed(() => typy(props.data, 'data.attributes.sql').isDefined)

const resultSetMap = computed(() => {
  if (typy(props.data, 'data.attributes.results').isDefined) {
    let map = {}
    let resSetCount = 0
    let resCount = 0
    for (const res of props.data.data.attributes.results) {
      if (typy(res, 'data').isDefined) {
        ++resSetCount
        map[`Result set ${resSetCount}`] = res
      } else if (typy(res, 'errno').isDefined) {
        map[ERR_TAB_ID] = res
      } else {
        ++resCount
        map[`Result ${resCount}`] = res
      }
    }
    return map
  } else return {}
})
const resultsetIds = computed(() => Object.keys(resultSetMap.value))
const activeResSet = computed(
  () => typy(resultSetMap.value[activeResultsetId.value]).safeObjectOrEmpty
)

watch(
  resultsetIds,
  (v) => {
    if (v.length) {
      const errResSetIdx = v.findIndex((id) => id === ERR_TAB_ID)
      activeResultsetId.value = errResSetIdx >= 0 ? v[errResSetIdx] : v[0]
    }
  },
  { deep: true }
)
</script>

<template>
  <QueryResultTabWrapper
    :dim="dim"
    :isLoading="isLoading"
    :showFooter="isLoading || hasRes"
    :resInfoBarProps="{ result: activeResSet, requestSentTime, execTime, totalDuration }"
  >
    <template #default="{ tblDim }">
      <i18n-t
        v-if="!isLoading && !hasRes"
        keypath="resultTabGuide"
        scope="global"
        tag="div"
        class="d-flex align-center pt-2"
      >
        <template #shortcut>
          &nbsp;
          <b>
            <kbd>{{ OS_KEY }}</kbd> + <kbd>SHIFT</kbd> + <kbd>ENTER</kbd>
          </b>
          &nbsp;
        </template>
        <template #icon>
          &nbsp;
          <VIcon color="primary" size="16" icon="mxs:running" />
          &nbsp;
        </template>
      </i18n-t>
      <KeepAlive v-else v-for="(resSet, name) in resultSetMap" :key="name" max="10">
        <DataTable
          v-if="activeResultsetId === name"
          :data="resSet"
          :height="tblDim.height"
          :width="tblDim.width"
          v-bind="dataTableProps"
        >
          <template #toolbar-left-append>
            <ResultSetItems
              v-if="resultsetIds.length"
              v-model="activeResultsetId"
              :items="resultsetIds"
              :errTabId="ERR_TAB_ID"
            />
          </template>
        </DataTable>
      </KeepAlive>
    </template>
  </QueryResultTabWrapper>
</template>
