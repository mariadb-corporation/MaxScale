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
import Worksheet from '@wsModels/Worksheet'
import worksheetService from '@/services/worksheetService'
import etlTaskService from '@/services/etlTaskService'
import { MIGR_DLG_TYPES } from '@/constants/workspace'

const store = useStore()

const migr_dlg = computed(() => store.state.workspace.migr_dlg)
const taskId = computed(() => migr_dlg.value.etl_task_id)

const isOpened = computed({
  get: () => {
    const { type, is_opened } = migr_dlg.value
    return type === MIGR_DLG_TYPES.DELETE ? is_opened : false
  },
  set: (v) => store.commit('workspace/SET_MIGR_DLG', { ...migr_dlg.value, is_opened: v }),
})

const etlTaskWke = computed(() => Worksheet.query().where('etl_task_id', taskId.value).first())

async function confirmDel() {
  if (etlTaskWke.value) await worksheetService.handleDelete(etlTaskWke.value.id)
  await etlTaskService.cascadeDelete(taskId.value)
}
</script>

<template>
  <BaseDlg
    v-model="isOpened"
    :onSave="confirmDel"
    :title="$t('confirmations.deleteEtl')"
    minBodyWidth="624px"
    :saveText="migr_dlg.type"
  >
    <template #form-body>
      <p class="mb-4">{{ $t('info.deleteEtlTask') }}</p>
    </template>
  </BaseDlg>
</template>
