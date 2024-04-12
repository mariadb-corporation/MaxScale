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
import DurationTimer from '@wkeComps/QueryEditor/DurationTimer.vue'
import ResultSetItems from '@wkeComps/QueryEditor/ResultSetItems.vue'
import ResultSetTable from '@wkeComps/QueryEditor/ResultSetTable.vue'
import IncompleteIndicator from '@wkeComps/QueryEditor/IncompleteIndicator.vue'
import { OS_KEY } from '@/constants/workspace'
import { useCommonResSetAttrs } from '@/composables/workspace'

const props = defineProps({
  dim: { type: Object, required: true },
  data: { type: Object, required: true },
  resultDataTableProps: { type: Object, required: true },
})

const typy = useTypy()

let headerRef = ref(null)
let headerHeight = ref(0)
let activeResultsetId = ref('')

const activeData = computed(() => props.data)

const { isLoading, requestSentTime, execTime, totalDuration } = useCommonResSetAttrs(activeData)

const queryTxt = computed(() => typy(props.data, 'data.attributes.sql').safeObject)
const ERR_TAB_ID = 'Error'
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
const resultTableHeight = computed(() => props.dim.height - headerHeight.value)

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
watch(isLoading, (v) => {
  if (!v) setHeaderHeight()
})
function setHeaderHeight() {
  if (headerRef.value) headerHeight.value = headerRef.value.clientHeight
}
</script>

<template>
  <div class="results-tab">
    <div ref="headerRef" class="pb-2 result-header d-flex align-center d-flex flex-row">
      <template v-if="!isLoading">
        <i18n-t
          v-if="!queryTxt"
          keypath="resultTabGuide"
          scope="global"
          tag="div"
          class="d-flex align-center"
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
        <GblTooltipActivator
          v-else
          :data="{ txt: queryTxt, interactive: true, offset: '0 16' }"
          class="mr-4 cursor--pointer text-anchor"
        >
          {{ $t('queryTxt') }}
        </GblTooltipActivator>
      </template>
      <ResultSetItems
        v-if="resultsetIds.length"
        v-model="activeResultsetId"
        :items="resultsetIds"
        :errTabId="ERR_TAB_ID"
      />
      <VSpacer />
      <DurationTimer
        v-if="requestSentTime"
        :startTime="requestSentTime"
        :executionTime="execTime"
        :totalDuration="totalDuration"
      />
      <IncompleteIndicator :resSet="$typy(resultSetMap[activeResultsetId]).safeObjectOrEmpty" />
    </div>
    <VSkeletonLoader
      v-if="isLoading"
      :loading="isLoading"
      type="table-tbody"
      :height="resultTableHeight"
    />
    <template v-else>
      <KeepAlive v-for="(resSet, name) in resultSetMap" :key="name" max="10">
        <template v-if="activeResultsetId === name">
          <ResultSetTable
            :data="resSet"
            :resultDataTableProps="resultDataTableProps"
            :height="resultTableHeight"
            :width="dim.width"
          />
        </template>
      </KeepAlive>
    </template>
  </div>
</template>
