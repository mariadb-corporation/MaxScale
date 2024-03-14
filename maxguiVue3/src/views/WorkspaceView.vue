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
import QueryConn from '@wsModels/QueryConn'
import Worksheet from '@wsModels/Worksheet'
import WorksheetTmp from '@wsModels/WorksheetTmp'
import ConfirmLeaveDlg from '@/components/workspace/ConfirmLeaveDlg.vue'
import ConnDlg from '@/components/workspace/ConnDlg.vue'
import MigrCreateDlg from '@/components/workspace/worksheets/DataMigration/MigrCreateDlg.vue'
import WorkspaceCtr from '@/components/workspace/WorkspaceCtr.vue'

const store = useStore()
const router = useRouter()

let isConfDlgOpened = ref(false)
let nextPath = ref('')

const conn_dlg = computed(() => store.state.mxsWorkspace.conn_dlg)
const allConns = computed(() => QueryConn.all())
const isConnDlgOpened = computed({
  get: () => conn_dlg.value.is_opened,
  set: (v) => store.commit('mxsWorkspace/SET_CONN_DLG', { ...conn_dlg.value, is_opened: v }),
})

onBeforeRouteLeave((to, from, next) => {
  if (nextPath.value) {
    next()
  } else {
    nextPath.value = to.path
    /**
     * Allow to leave page immediately if next path is to login page (user logouts)
     * or if there is no active connections
     */
    if (allConns.value.length === 0) leavePage()
    else if (to.path === '/login') leavePage()
    else isConfDlgOpened.value = true
  }
})
onBeforeMount(async () => {
  await store.dispatch('mxsWorkspace/initWorkspace')
  Worksheet.all().forEach((wke) => {
    WorksheetTmp.update({ where: wke.id, data: { request_config: { baseURL: '/api' } } })
  })
  await QueryConn.dispatch('validateConns')
})

async function onConfirm(shouldDelAll) {
  if (shouldDelAll) await QueryConn.dispatch('disconnectAll')
  leavePage()
}

function leavePage() {
  router.push(nextPath.value)
}

function cancelLeave() {
  nextPath.value = null
}

function createEtlTask(name) {
  EtlTask.dispatch('createEtlTask', name)
}
</script>

<template>
  <div class="workspace-view fill-height">
    <WorkspaceCtr />
    <ConfirmLeaveDlg
      v-model="isConfDlgOpened"
      :confirm="onConfirm"
      @after-close="cancelLeave"
      @after-cancel="cancelLeave"
    />
    <ConnDlg v-model="isConnDlgOpened" />
    <MigrCreateDlg :handleSave="createEtlTask" />
  </div>
</template>
