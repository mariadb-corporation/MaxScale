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
import EtlTask from '@wsModels/EtlTask'
import QueryTab from '@wsModels/QueryTab'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import worksheetService from '@/services/worksheetService'
import { ETL_STATUS } from '@/constants/workspace'

const props = defineProps({ wke: { type: Object, required: true } })
const typy = useTypy()

const wkeId = computed(() => props.wke.id)
const isRunningETL = computed(() => {
  const etlTask = EtlTask.find(typy(props.wke, 'etl_task_id').safeString)
  return typy(etlTask, 'status').safeString === ETL_STATUS.RUNNING
})
const queryTabs = computed(
  () =>
    QueryTab.query()
      .where((t) => t.query_editor_id === wkeId.value)
      .get() || []
)
const isOneOfQueryTabsRunning = computed(() =>
  queryTabs.value.some(
    ({ id }) => typy(QueryTabTmp.find(id), 'query_results.is_loading').safeBoolean
  )
)
const isRunning = computed(() => isOneOfQueryTabsRunning.value || isRunningETL.value)

async function onDelete() {
  await worksheetService.handleDelete(props.wke.id)
}
</script>

<template>
  <VHover>
    <template #default="{ isHovering, props }">
      <span
        :style="{ width: '162px' }"
        class="fill-height d-flex align-center justify-space-between px-3"
        v-bind="props"
      >
        <GblTooltipActivator
          :data="{ txt: wke.name }"
          activateOnTruncation
          :maxWidth="120"
          fillHeight
        />
        <VProgressCircular
          v-if="isRunning"
          class="ml-2"
          size="16"
          width="2"
          color="primary"
          indeterminate
        />
        <VBtn
          v-else
          v-show="isHovering"
          variant="text"
          icon
          size="small"
          density="compact"
          @click.stop.prevent="onDelete"
        >
          <VIcon :size="8" color="error" icon="mxs:close" />
        </VBtn>
      </span>
    </template>
  </VHover>
</template>
