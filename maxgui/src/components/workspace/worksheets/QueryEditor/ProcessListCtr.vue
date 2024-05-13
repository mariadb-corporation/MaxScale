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
import workspaceService from '@wsServices/workspaceService'

const props = defineProps({
  dim: { type: Object, required: true },
  data: { type: Object, required: true },
  queryTabConn: { type: Object, required: true },
  resultDataTableProps: { type: Object, required: true },
  isLoading: { type: Boolean, required: true },
})

const store = useStore()
const typy = useTypy()

const selectedItems = ref([])

const exec_sql_dlg = computed(() => store.state.workspace.exec_sql_dlg)
const resultset = computed(() => typy(props.data, 'data.attributes.results[0]').safeObjectOrEmpty)
const fieldIdxMap = computed(() =>
  typy(resultset.value, 'fields').safeArray.reduce((map, field, i) => ((map[field] = i), map), {})
)
const defHiddenHeaderIndexes = computed(() => {
  const fields = [
    'TIME_MS',
    'STAGE',
    'MAX_STAGE',
    'MEMORY_USED',
    'MAX_MEMORY_USED',
    'EXAMINED_ROWS',
    'QUERY_ID',
    'INFO_BINARY',
    'TID',
  ]
  // plus 1 as ResultSetTable automatically adds `#` column
  return fields.map((field) => fieldIdxMap.value[field] + 1)
})

watch(
  () => props.queryTabConn.id,
  async (v) => {
    if (v) await fetch()
  },
  { immediate: true }
)

function resetSelectedItems() {
  selectedItems.value = []
}

async function fetch() {
  resetSelectedItems()
  await queryResultService.queryProcessList()
}

async function confirmExeStatements() {
  await workspaceService.exeStatement({
    connId: props.queryTabConn.id,
    sql: exec_sql_dlg.value.sql,
    action:
      selectedItems.value.length === 1
        ? `Kill thread ${typy(selectedItems.value, '[0][1]').safeNumber}`
        : 'Kill threads',
  })
  resetSelectedItems()
  await fetch()
}

function handleOpenExecSqlDlg() {
  store.commit('workspace/SET_EXEC_SQL_DLG', {
    ...exec_sql_dlg.value,
    is_opened: true,
    editor_height: 200,
    sql: selectedItems.value.map((row) => `KILL ${row[1]}`).join(';\n'),
    on_exec: confirmExeStatements,
    after_cancel: resetSelectedItems,
  })
}
</script>

<template>
  <div class="process-list-ctr pt-2">
    <VProgressLinear v-if="isLoading" indeterminate color="primary" />
    <ResultSetTable
      v-else
      v-model:selectedItems="selectedItems"
      :data="resultset"
      :resultDataTableProps="{
        ...resultDataTableProps,
        defHiddenHeaderIndexes,
        showSelect: true,
        deleteItemBtnLabel: 'kill',
        deleteItemBtnTooltipTxt: 'killThreads',
      }"
      :height="dim.height"
      :width="dim.width"
      @on-delete="handleOpenExecSqlDlg"
    >
      <template #left-table-tools-append>
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
      <template #right-table-tools-prepend>
        <IncompleteIndicator class="mx-2" :resSet="resultset" />
      </template>
    </ResultSetTable>
  </div>
</template>
