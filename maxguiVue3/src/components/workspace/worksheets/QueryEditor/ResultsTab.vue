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
import ResultDataTable from '@wkeComps/QueryEditor/ResultDataTable.vue'
import { OS_KEY } from '@/constants/workspace'

const props = defineProps({
  dim: { type: Object, required: true },
  isLoading: { type: Boolean, required: true },
  data: { type: Object, required: true },
  requestSentTime: { type: Number, required: true },
  execTime: { type: Number, required: true },
  totalDuration: { type: Number, required: true },
  resultDataTableAttrs: { type: Object, default: () => ({}) },
})

const typy = useTypy()

let headerRef = ref(null)
let headerHeight = ref(0)
let activeResultsetId = ref('')

const queryTxt = computed(() => typy(props.data, 'data.attributes.sql').safeObject)
const ERR_TAB_ID = 'Error'
const resultData = computed(() => {
  if (typy(props.data, 'data.attributes.results').isDefined) {
    let resultData = {}
    let resSetCount = 0
    let resCount = 0
    for (const res of props.data.data.attributes.results) {
      if (typy(res, 'data').isDefined) {
        ++resSetCount
        resultData[`Result set ${resSetCount}`] = {
          headers: typy(res, 'fields').safeArray.map((field) => ({
            text: field,
          })),
          rows: typy(res, 'data').safeArray,
          complete: typy(res, 'complete').safeBoolean,
          metadata: typy(res, 'metadata').safeArray,
        }
      } else if (typy(res, 'errno').isDefined) {
        resultData[ERR_TAB_ID] = res
      } else {
        ++resCount
        resultData[`Result ${resCount}`] = res
      }
    }
    return resultData
  } else return {}
})
const resultsetIds = computed(() => Object.keys(resultData.value))
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

watch(
  () => props.isLoading,
  (v) => {
    if (!v) setHeaderHeight()
  }
)
function setHeaderHeight() {
  if (headerRef.value) headerHeight.value = headerRef.value.clientHeight
}
</script>

<template>
  <div class="fill-height">
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
          :debounce="0"
        >
          <span class="mr-4 pointer text-anchor"> {{ $t('queryTxt') }} </span>
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
      <VTooltip
        v-if="
          $typy(resultData[activeResultsetId], 'headers').isDefined &&
          !resultData[activeResultsetId].complete
        "
        location="top"
        transition="slide-y-transition"
      >
        <template #activator="{ props }">
          <div class="ml-4 d-flex align-center" v-bind="props">
            <VIcon size="16" color="error" class="mr-2" icon="mxs:alertWarning" />
            {{ $t('incomplete') }}
          </div>
        </template>
        {{ $t('info.queryIncomplete') }}
      </VTooltip>
    </div>
    <VSkeletonLoader
      v-if="isLoading"
      :loading="isLoading"
      type="table-tbody"
      :height="resultTableHeight"
    />
    <template v-else>
      <KeepAlive v-for="(resSet, name) in resultData" :key="name">
        <template v-if="activeResultsetId === name">
          <ResultDataTable
            v-if="$typy(resSet, 'headers').isDefined"
            :height="resultTableHeight"
            :width="dim.width"
            :headers="resSet.headers"
            :data="resSet.rows"
            :metadata="resSet.metadata"
            showGroupBy
            v-bind="resultDataTableAttrs"
          />
          <div v-else :style="{ height: `${resultTableHeight}px` }">
            <div v-for="(v, key) in resSet" :key="key">
              <b>{{ key }}:</b>
              <span class="d-inline-block ml-4">{{ v }}</span>
            </div>
          </div>
        </template>
      </KeepAlive>
    </template>
  </div>
</template>
