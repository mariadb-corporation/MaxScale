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
import ResultSetTable from '@wkeComps/QueryEditor/ResultSetTable.vue'
import IncompleteIndicator from '@wkeComps/QueryEditor/IncompleteIndicator.vue'
import queryResultService from '@/services/workspace/queryResultService'

const props = defineProps({
  dim: { type: Object, required: true },
  data: { type: Object, required: true },
  queryTabConn: { type: Object, required: true },
  resultDataTableProps: { type: Object, required: true },
  isLoading: { type: Boolean, required: true },
})
const typy = useTypy()

const resultset = computed(() => typy(props.data, 'data.attributes.results[0]').safeObjectOrEmpty)

watch(
  () => props.queryTabConn.id,
  async (v) => {
    if (v) await fetch()
  },
  { immediate: true }
)

async function fetch() {
  await queryResultService.queryProcessList()
}
</script>

<template>
  <div class="process-list-ctr pt-2">
    <VProgressLinear v-if="isLoading" indeterminate color="primary" />
    <ResultSetTable
      v-else
      :data="resultset"
      :resultDataTableProps="resultDataTableProps"
      :height="dim.height"
      :width="dim.width"
    >
      <template #right-table-tools-prepend>
        <IncompleteIndicator :resSet="resultset" />
        <TooltipBtn
          class="mx-2"
          size="small"
          :width="36"
          :min-width="'unset'"
          density="comfortable"
          color="primary"
          variant="outlined"
          :disabled="isLoading"
          @click="fetch"
        >
          <template #btn-content>
            <VIcon size="14" icon="mxs:reload" />
          </template>
          {{ $t('reload') }}
        </TooltipBtn>
      </template>
    </ResultSetTable>
  </div>
</template>
