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
import ResultSetTable from '@wkeComps/QueryEditor/ResultSetTable.vue'
import IncompleteIndicator from '@wkeComps/QueryEditor/IncompleteIndicator.vue'
import queryResultService from '@/services/workspace/queryResultService'
import workspaceService from '@wsServices/workspaceService'
import QueryConn from '@wsModels/QueryConn'
import Worksheet from '@wsModels/Worksheet'
import { http } from '@/utils/axios'
import { PROCESS_TYPES } from '@/constants/workspace'
import { MXS_OBJ_TYPES } from '@/constants'

const props = defineProps({
  dim: { type: Object, required: true },
  data: { type: Object, required: true },
  queryTabConn: { type: Object, required: true },
  resultDataTableProps: { type: Object, required: true },
  isLoading: { type: Boolean, required: true },
})

const store = useStore()
const typy = useTypy()
const {
  tryAsync,
  lodash: { isEqual, cloneDeep, groupBy },
} = useHelpers()
const { t } = useI18n()
const { SERVICES, LISTENERS, SERVERS } = MXS_OBJ_TYPES

const selectedItems = ref([])
const processTypesToShow = ref([])
const sessions = ref([])
const isConfDlgOpened = ref(false)
const selectedSessions = ref([])

const reqConfig = computed(() => Worksheet.getters('activeRequestConfig'))
const exec_sql_dlg = computed(() => store.state.workspace.exec_sql_dlg)
const wsConns = computed(() => QueryConn.all())
const wsConnGroupedByType = computed(() =>
  groupBy(wsConns.value, (conn) => typy(conn, 'meta.type').safeString)
)
const serviceAndListenerConns = computed(() =>
  [
    typy(wsConnGroupedByType.value[SERVICES]).safeArray,
    typy(wsConnGroupedByType.value[LISTENERS]).safeArray,
  ].flat()
)
const sessionIds = computed(() =>
  serviceAndListenerConns.value.map((conn) => typy(conn, 'attributes.thread_id').safeNumber)
)
const connectionIdToSessionMap = computed(() =>
  sessions.value.reduce((map, session) => {
    typy(session, 'attributes.connections').safeArray.forEach((conn) => {
      map[conn.connection_id] = session
    })
    return map
  }, {})
)
const sessionConnIds = computed(() => Object.keys(connectionIdToSessionMap.value).map(Number))
const wsProcessIds = computed(() =>
  [
    ...sessionConnIds.value,
    typy(wsConnGroupedByType.value[SERVERS]).safeArray.map(
      (conn) => typy(conn, 'attributes.thread_id').safeNumber
    ),
  ].flat()
)

const resultset = computed(() => {
  let result = cloneDeep(typy(props.data, 'data.attributes.results[0]').safeObjectOrEmpty)
  if (processTypesToShow.value.length === 2 || processTypesToShow.value.length === 0) return result
  const data = typy(result, 'data').safeArray
  if (data.length && processTypesToShow.value.length === 1) {
    const type = processTypesToShow.value[0]
    result.data = data.filter((row) =>
      type === PROCESS_TYPES.WORKSPACE
        ? wsProcessIds.value.includes(row[0])
        : !wsProcessIds.value.includes(row[0])
    )
  }

  return result
})
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
  // plus 1 as ResultSetTable automatically adds `#` column which is index 0
  return [0, ...fields.map((field) => fieldIdxMap.value[field] + 1)]
})
const connId = computed(() => typy(props.queryTabConn, 'id').safeString)

watch(
  sessionIds,
  async (v, oV) => {
    if (!isEqual(v, oV)) {
      let items = []
      for (const id of v) {
        const session = await fetchSession(id)
        if (session.id) items.push(session)
      }
      sessions.value = items
    }
  },
  { immediate: true, deep: true }
)

onActivated(async () => {
  if (connId.value && typy(resultset.value).isEmptyObject) await fetch()
})

function resetSelectedItems() {
  selectedItems.value = []
}

async function fetch() {
  resetSelectedItems()
  await queryResultService.queryProcessList()
}

async function confirmExeStatements() {
  await workspaceService.exeStatement({
    connId: connId.value,
    sql: exec_sql_dlg.value.sql,
    action:
      selectedItems.value.length === 1
        ? `Kill process ${typy(selectedItems.value, '[0][1]').safeNumber}`
        : 'Kill processes',
  })
  resetSelectedItems()
  await fetch()
}

function handleOpenExecSqlDlg() {
  let selectedSessionConnIds = [],
    sql = ''
  selectedItems.value.forEach((row) => {
    if (sessionConnIds.value.includes(row[1])) selectedSessionConnIds.push(row[1])
    else sql += `KILL ${row[1]};\n`
  })
  selectedSessions.value = selectedSessionConnIds.map((id) => {
    const session = connectionIdToSessionMap.value[id]
    return {
      id: session.id,
      connection: typy(session.attributes.connections, '[0]').safeObjectOrEmpty,
      service: typy(session.relationships, 'services.data[0].id').safeString,
    }
  })
  if (sql)
    store.commit('workspace/SET_EXEC_SQL_DLG', {
      ...exec_sql_dlg.value,
      is_opened: true,
      editor_height: 200,
      sql,
      extra_info: selectedSessionConnIds.length
        ? t('info.killProcessesThatHasSessions', [selectedSessionConnIds.join(', ')])
        : '',
      on_exec: async () => {
        await confirmExeStatements()
        await killSessions()
      },
      after_cancel: resetSelectedItems,
    })
  else if (selectedSessionConnIds.length) {
    isConfDlgOpened.value = true
  }
}

async function fetchSession(id) {
  const [, res] = await tryAsync(
    http.get(`/sessions/${id}`, { ...reqConfig.value, metadata: { ignore404: true } })
  )
  return typy(res, 'data.data').safeObjectOrEmpty
}

async function killSessions() {
  const [, allRes = []] = await tryAsync(
    Promise.all(
      selectedSessions.value.map((session) =>
        http.delete(`/sessions/${session.id}`, {
          ...reqConfig.value,
          metadata: { ignore404: true },
        })
      )
    )
  )
  if (allRes.length && allRes.every((promise) => promise.status === 200)) {
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
      text: [t('success.killedSessions', { count: allRes.length })],
      type: 'success',
    })
    await fetch()
  }
  selectedSessions.value = []
}
</script>

<template>
  <div class="process-list-ctr pt-2">
    <VProgressLinear v-if="isLoading" indeterminate color="primary" />
    <template v-else-if="!connId">{{ $t('processListNoConn') }}</template>
    <ResultSetTable
      v-else
      v-model:selectedItems="selectedItems"
      :data="resultset"
      :resultDataTableProps="{
        ...resultDataTableProps,
        defHiddenHeaderIndexes,
        showSelect: true,
        deleteItemBtnLabel: 'kill',
        deleteItemBtnTooltipTxt: 'killProcess',
      }"
      :height="dim.height"
      :width="dim.width"
      @on-delete="handleOpenExecSqlDlg"
    >
      <template #left-table-tools-append>
        <FilterList
          v-model="processTypesToShow"
          :label="$t('processTypes')"
          :items="Object.values(PROCESS_TYPES)"
          :maxHeight="200"
          hideSelectAll
          hideSearch
          :activatorProps="{ size: 'small', density: 'comfortable' }"
          activatorClass="ml-2"
        />
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
      <template #result-error-append>
        <VBtn
          class="mt-4"
          size="small"
          density="comfortable"
          color="primary"
          variant="outlined"
          :disabled="isLoading"
          @click="fetch"
        >
          {{ $t('reload') }}
        </VBtn>
      </template>
    </ResultSetTable>
    <BaseDlg
      v-model="isConfDlgOpened"
      :title="$t('killSessions', { count: selectedSessions.length })"
      saveText="kill"
      minBodyWidth="768px"
      :onSave="killSessions"
    >
      <template v-if="selectedSessions.length" #form-body>
        <p class="confirmations-text">
          {{ $t(`confirmations.killSessions`, { count: selectedSessions.length }) }}
        </p>
        <TreeTable
          v-for="session in selectedSessions"
          :key="session.id"
          :data="session"
          hideHeader
          expandAll
          density="compact"
          class="my-4"
        />
      </template>
    </BaseDlg>
  </div>
</template>
