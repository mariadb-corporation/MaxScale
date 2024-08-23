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
import QueryConn from '@wsModels/QueryConn'
import Worksheet from '@wsModels/Worksheet'
import EtlTask from '@wsModels/EtlTask'
import queryEditorService from '@wsServices/queryEditorService'
import etlTaskService from '@wsServices/etlTaskService'
import queryConnService from '@wsServices/queryConnService'
import { ETL_STAGE_INDEX_MAP } from '@/constants/workspace'

const typy = useTypy()

const activeWke = computed(() => Worksheet.getters('activeRecord'))
const isActiveQueryEditorWke = computed(() => Boolean(activeWke.value.query_editor_id))
const isActiveEtlWke = computed(() => Boolean(activeWke.value.etl_task_id))
const activeEtlTask = computed(() => EtlTask.getters('activeRecord'))
const activeQueryEditorConn = computed(() => QueryConn.getters('activeQueryEditorConn'))
const activeConns = computed(() => {
  if (isActiveQueryEditorWke.value)
    return [activeQueryEditorConn.value, QueryConn.getters('activeQueryTabConn')]
  if (isActiveEtlWke.value) return QueryConn.getters('activeEtlConns')
  return []
})
const lostConns = computed(() =>
  activeConns.value.reduce((arr, c) => {
    if (typy(c, 'lost_cnn_err.message').safeString) arr.push(c)
    return arr
  }, [])
)
const lostConnIds = computed(() => lostConns.value.map((c) => c.id))
const isWkeConnLost = computed(() => lostConnIds.value.includes(activeQueryEditorConn.value.id))
const connIdsToBeReconnected = computed(() => {
  const ids = lostConnIds.value
  /**
   * QueryEditor connection is normally not included in 'lostConns'
   * since the `lost_cnn_err` is retrieved if the QueryEditor connection
   * is used for querying. The QueryEditor connection, on the other hand,
   * is solely utilized to terminate the running query. As a result,
   * it's preferable to provide the QueryEditor connection for reconnecting
   * to avoid this edge case.
   */
  if (isActiveQueryEditorWke.value && !isWkeConnLost.value) ids.push(activeQueryEditorConn.value.id)
  return ids
})
const showReconnDialog = computed({
  get() {
    return Boolean(lostConns.value.length)
  },
  set() {
    connIdsToBeReconnected.value.forEach((id) =>
      QueryConn.update({ where: id, data: { lost_cnn_err: {} } })
    )
  },
})

async function deleteConns() {
  if (isActiveQueryEditorWke.value)
    await queryConnService.cascadeDisconnect({ id: activeQueryEditorConn.value.id })
  else
    await Promise.all(connIdsToBeReconnected.value.map((id) => queryConnService.disconnect({ id })))
}

async function handleReconnect() {
  await queryConnService.reconnectConns({
    ids: connIdsToBeReconnected.value,
    onError: async () => await deleteConns(),
    onSuccess: async () => {
      if (isActiveQueryEditorWke.value) await queryEditorService.initialFetch()
      else if (activeEtlTask.value.active_stage_index === ETL_STAGE_INDEX_MAP.SRC_OBJ)
        await etlTaskService.fetchSrcSchemas()
    },
  })
}
</script>

<template>
  <BaseDlg
    v-model="showReconnDialog"
    :title="$t('errors.serverHasGoneAway')"
    minBodyWidth="624px"
    :onSave="handleReconnect"
    cancelText="disconnect"
    saveText="reconnect"
    :showCloseBtn="false"
    @after-cancel="deleteConns"
  >
    <template #form-body>
      <table v-if="showReconnDialog" class="tbl-code lost-conn-tbl pa-4">
        <tbody v-for="(v, i) in lostConns" :key="i">
          <tr>
            <td class="font-weight-bold">{{ $t('servers', 1) }}</td>
            <td>{{ v.meta.name }}</td>
          </tr>
          <tr v-for="(errValue, errField) in v.lost_cnn_err" :key="errField">
            <td class="font-weight-bold">{{ errField }}</td>
            <td>{{ errValue }}</td>
          </tr>
        </tbody>
      </table>
    </template>
  </BaseDlg>
</template>

<style lang="scss" scoped>
.lost-conn-tbl {
  tbody {
    &:not(:last-of-type) {
      &::after,
      &:first-of-type::before {
        content: '';
        display: block;
        height: 12px;
      }
    }
  }
}
</style>
