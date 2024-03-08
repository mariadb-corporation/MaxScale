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
import LogFilters from '@/components/logsArchive/LogFilters.vue'
import LogViewer from '@/components/logsArchive/LogViewer.vue'

const typy = useTypy()

let logViewHeight = ref(0)
let containerRef = ref(null)
let logFiltersRef = ref(null)
function setPageContentDim() {
  nextTick(() => {
    const pageContentHeight =
      typy(containerRef.value, 'clientHeight').safeNumber -
      typy(logFiltersRef.value, '$el.clientHeight').safeNumber
    // 32 is vertical padding in px
    if (pageContentHeight) logViewHeight.value = pageContentHeight - 32
  })
}
</script>

<template>
  <ViewWrapper v-resize="setPageContentDim" :overflow="false" fluid class="fill-height">
    <div ref="containerRef" class="fill-height">
      <LogFilters ref="logFiltersRef" class="pb-2" />
      <div v-if="logViewHeight" class="log-lines-container pa-4 bg-separator">
        <LogViewer :height="logViewHeight" />
      </div>
    </div>
  </ViewWrapper>
</template>
