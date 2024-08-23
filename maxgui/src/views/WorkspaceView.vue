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
import WorksheetTmp from '@wsModels/WorksheetTmp'
import ConfirmLeaveDlg from '@wsComps/ConfirmLeaveDlg.vue'
import ConnDlg from '@wsComps/ConnDlg.vue'
import MigrCreateDlg from '@wsComps/worksheets/DataMigration/MigrCreateDlg.vue'
import WorkspaceCtr from '@wsComps/WorkspaceCtr.vue'
import workspaceService from '@wsServices/workspaceService'
import etlTaskService from '@wsServices/etlTaskService'
import queryConnService from '@wsServices/queryConnService'
import { queryHttp } from '@/utils/axios'

const store = useStore()
const router = useRouter()

const isConfDlgOpened = ref(false)
const nextPath = ref('')

const isValidatingConn = ref(true)
const conn_dlg = computed(() => store.state.workspace.conn_dlg)
const allConns = computed(() => QueryConn.all())
const hasConns = computed(() => Boolean(allConns.value.length))
const isConnDlgOpened = computed({
  get: () => conn_dlg.value.is_opened,
  set: (v) => store.commit('workspace/SET_CONN_DLG', { ...conn_dlg.value, is_opened: v }),
})
const show_confirm_dlg_before_leave = computed(
  () => store.state.prefAndStorage.show_confirm_dlg_before_leave
)
const del_all_conns_before_leave = computed(
  () => store.state.prefAndStorage.del_all_conns_before_leave
)

onBeforeRouteLeave((to, from, next) => {
  if (nextPath.value) {
    next()
  } else {
    nextPath.value = to.path
    /**
     * Allow to leave page immediately if next path is to login page (user logouts)
     * or if there is no active connections
     */
    if (!hasConns.value || to.path === '/login' || !show_confirm_dlg_before_leave.value) {
      if (hasConns.value && del_all_conns_before_leave.value) queryConnService.disconnectAll()
      leavePage()
    } else isConfDlgOpened.value = true
  }
})
onBeforeMount(async () => {
  await workspaceService.init()
  Worksheet.all().forEach((wke) => {
    WorksheetTmp.update({
      where: wke.id,
      data: { request_config: { baseURL: queryHttp.defaults.baseURL } },
    })
  })
  await queryConnService.validateConns()
  isValidatingConn.value = false
})

async function onConfirm(shouldDelAll) {
  if (shouldDelAll) await queryConnService.disconnectAll()
  leavePage()
}

function leavePage() {
  router.push(nextPath.value)
}

function cancelLeave() {
  nextPath.value = null
}

function createEtlTask(name) {
  etlTaskService.create(name)
}
</script>

<template>
  <div class="workspace-view fill-height">
    <VProgressLinear v-if="isValidatingConn" indeterminate color="primary" />
    <WorkspaceCtr v-else />
    <ConfirmLeaveDlg
      v-if="show_confirm_dlg_before_leave"
      v-model="isConfDlgOpened"
      :confirm="onConfirm"
      @after-close="cancelLeave"
      @after-cancel="cancelLeave"
    />
    <ConnDlg v-model="isConnDlgOpened" />
    <MigrCreateDlg :handleSave="createEtlTask" />
  </div>
</template>
