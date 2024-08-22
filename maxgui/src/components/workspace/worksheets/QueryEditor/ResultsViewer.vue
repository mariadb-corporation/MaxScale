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
import ResultSetItems from '@wkeComps/QueryEditor/ResultSetItems.vue'
import ResultView from '@/components/workspace/worksheets/QueryEditor/ResultView.vue'
import queryResultService from '@wsServices/queryResultService'
import workspace from '@/composables/workspace'
import { OS_CMD, QUERY_CANCELED, QUERY_LOG_TYPE_MAP } from '@/constants/workspace'

const props = defineProps({
  dim: { type: Object, required: true },
  data: { type: Object, required: true },
  dataTableProps: { type: Object, required: true },
})

const typy = useTypy()
const { immutableUpdate } = useHelpers()
const ERR_RES_PREFIX = 'Error result'
const QUERY_CANCELED_PREFIX = 'Query canceled'

const activeQueryResId = ref('')
const isReloading = ref(false)

const resultData = computed(() => props.data)
const { startTime, endTime } = workspace.useCommonResSetAttrs(resultData)
const { count } = useElapsedTimer(startTime, endTime)

const isLoading = computed(() => typy(props.data, 'is_loading').safeBoolean)
const hasStatements = computed(() => typy(props.data, 'statements').isDefined)
const showGuide = computed(() => !hasStatements.value)

const queryResMap = computed(() => {
  const map = {}
  let resSetCount = 0,
    resCount = 0,
    errCount = 0,
    canceledCount = 0
  typy(props.data, 'data').safeArray.forEach((stmtResults) => {
    typy(stmtResults, 'data.attributes.results').safeArray.forEach((result) => {
      const res = immutableUpdate(stmtResults, {
        data: { attributes: { results: { $set: [result] } } },
      })
      if (typy(result, 'data').isDefined) {
        ++resSetCount
        map[`Result set ${resSetCount}`] = res
      } else if (typy(result, 'errno').isDefined) {
        ++errCount
        map[`${ERR_RES_PREFIX} ${errCount}`] = res
      } else if (typy(result, 'message').safeString === QUERY_CANCELED) {
        ++canceledCount
        map[`${QUERY_CANCELED_PREFIX} ${canceledCount}`] = res
      } else {
        ++resCount
        map[`Result ${resCount}`] = res
      }
    })
  })
  return map
})

const queryResIds = computed(() => Object.keys(queryResMap.value))

// Watch on result ids to automatically choose the active one
watch(
  queryResIds,
  (v) => {
    if (v.length && !isReloading.value) {
      const priorityItemIdx = findPriorityIndex()
      activeQueryResId.value = priorityItemIdx >= 0 ? v[priorityItemIdx] : v[0]
    }
  },
  { deep: true }
)

function findPriorityIndex() {
  const prefixes = [ERR_RES_PREFIX, QUERY_CANCELED_PREFIX]
  for (let i = 0; i < queryResIds.value.length; i++) {
    const id = queryResIds.value[i]
    if (prefixes.some((prefix) => id.includes(prefix))) return i
  }
  return -1
}
async function reload({ statement, index }) {
  isReloading.value = true
  await queryResultService.query({
    statement,
    maxRows: statement.limit,
    path: ['query_results', 'data', index],
    queryType: QUERY_LOG_TYPE_MAP.USER_LOGS,
  })
  activeQueryResId.value = queryResIds.value[index]
  isReloading.value = false
}
</script>

<template>
  <div class="results-viewer pos--relative">
    <div v-if="showGuide || isLoading" class="pt-2 px-5">
      <i18n-t
        v-if="showGuide"
        keypath="resultTabGuide"
        scope="global"
        tag="div"
        class="d-flex align-center flex-wrap"
      >
        <template #shortcut>
          &nbsp;
          <b>
            <kbd>{{ OS_CMD }}</kbd> + <kbd>SHIFT</kbd> + <kbd>ENTER</kbd>
          </b>
          &nbsp;
        </template>
        <template #icon>
          <VIcon color="primary" size="16" icon="mxs:running" class="mx-1" />
        </template>
      </i18n-t>
      <template v-else-if="isLoading">
        <VProgressLinear indeterminate color="primary" />
        <div class="mt-2">{{ Math.round(count) }} sec</div>
      </template>
    </div>
    <KeepAlive v-else v-for="(res, id, index) in queryResMap" :key="id">
      <ResultView
        v-if="activeQueryResId === id"
        :data="res"
        :dim="dim"
        :dataTableProps="dataTableProps"
        :reload="async (statement) => await reload({ statement, index })"
        class="fill-height"
      >
        <template #toolbar-left-append>
          <ResultSetItems
            v-model="activeQueryResId"
            :items="queryResIds"
            :errResPrefix="ERR_RES_PREFIX"
            :queryCanceledPrefix="QUERY_CANCELED_PREFIX"
          />
        </template>
      </ResultView>
    </KeepAlive>
  </div>
</template>
