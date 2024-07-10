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
import DataTable from '@/components/workspace/worksheets/QueryEditor/DataTable.vue'
import workspace from '@/composables/workspace'

const props = defineProps({
  data: { type: Object, required: true },
  dataTableProps: { type: Object, required: true },
  dim: { type: Object, required: true },
})

const typy = useTypy()

const queryData = computed(() => props.data)
const hasRes = computed(() => typy(queryData.value, 'data.attributes.sql').isDefined)
const resultset = computed(
  () => typy(queryData.value, 'data.attributes.results[0]').safeObjectOrEmpty
)
const { isLoading, requestSentTime, execTime, totalDuration } =
  workspace.useCommonResSetAttrs(queryData)
</script>

<template>
  <QueryResultTabWrapper
    :dim="dim"
    :isLoading="isLoading"
    :showFooter="isLoading || hasRes"
    :resInfoBarProps="{ result: resultset, requestSentTime, execTime, totalDuration }"
  >
    <template #default="{ tblDim }">
      <DataTable
        :data="resultset"
        :height="tblDim.height"
        :width="tblDim.width"
        v-bind="dataTableProps"
      >
        <template v-for="(_, name) in $slots" #[name]="slotData">
          <slot :name="name" v-bind="slotData" />
        </template>
      </DataTable>
    </template>
  </QueryResultTabWrapper>
</template>
