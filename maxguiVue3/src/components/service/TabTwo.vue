<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

const props = defineProps({
  obj_data: { type: Object, required: true },
  routingTargetItems: { type: Array, required: true },
  fetchSessions: { type: Function, required: true },
})

const typy = useTypy()
const { dateFormat } = useHelpers()

const isKafkacdc = computed(
  () => typy(props.obj_data, 'attributes.router').safeString === 'kafkacdc'
)

const store = useStore()
const filtered_sessions = computed(() => store.state.sessions.filtered_sessions)
const total_filtered_sessions = computed(() => store.state.sessions.total_filtered_sessions)

const serviceStats = computed(() => typy(props.obj_data, 'attributes.statistics').safeObjectOrEmpty)
const routerDiagnostics = computed(() => {
  let data = typy(props.obj_data, 'attributes.router_diagnostics').safeObjectOrEmpty
  if (isKafkacdc.value) {
    const targetServer = props.routingTargetItems.find((row) => row.id === data.target)
    data.gtid_current_pos = typy(targetServer, 'gtid_current_pos').safeString
  }
  return data
})

const sessionItems = computed(() =>
  filtered_sessions.value.map((session) => {
    const {
      id,
      attributes: { idle, connected, user, remote, memory, io_activity },
    } = session
    return {
      id: id,
      user: `${user}@${remote}`,
      connected: dateFormat({ value: connected }),
      idle,
      memory,
      io_activity,
    }
  })
)

async function killSession(id) {
  await store.dispatch('sessions/killSession', { id, callback: props.fetchSessions })
}
</script>

<template>
  <VRow>
    <VCol cols="5">
      <VRow>
        <VCol cols="12">
          <CollapsibleReadOnlyTbl :title="`${$t('statistics')}`" :data="serviceStats" expandAll />
        </VCol>
        <VCol cols="12">
          <CollapsibleReadOnlyTbl
            :title="`${$t('routerDiagnostics')}`"
            :data="routerDiagnostics"
            expandAll
          />
        </VCol>
      </VRow>
    </VCol>
    <VCol cols="7">
      <CollapsibleCtr :title="`${$t('currentSessions', 2)}`" :titleInfo="sessionItems.length">
        <SessionsTable
          :items="sessionItems"
          :items-length="total_filtered_sessions"
          @confirm-kill="killSession"
          @on-update="fetchSessions"
        />
      </CollapsibleCtr>
    </VCol>
  </VRow>
</template>
