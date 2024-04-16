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
import sessionsService from '@/services/sessionsService'
import { MXS_OBJ_TYPES } from '@/constants'

const store = useStore()
const { t } = useI18n()
const typy = useTypy()
const { dateFormat } = useHelpers()

const servicesLength = ref(0)

const SERVICE_HEADER = {
  title: 'Service',
  value: 'serviceId',
  autoTruncate: true,
  cellProps: { class: 'pa-0' },
  customRender: {
    renderer: 'AnchorLink',
    objType: MXS_OBJ_TYPES.SERVICES,
    props: { class: 'px-6' },
  },
}
const current_sessions = computed(() => store.state.sessions.current_sessions)
const total_sessions = computed(() => store.state.sessions.total_sessions)
const items = computed(() => {
  let rows = []
  let allServiceNames = []
  current_sessions.value.forEach((session) => {
    const {
      id,
      attributes: { idle, connected, user, remote, memory, io_activity },
      relationships: { services: { data: associatedServices = [] } = {} },
    } = session || {}

    const serviceId = associatedServices.length
      ? typy(associatedServices[0], 'id').safeString
      : t('noEntity', MXS_OBJ_TYPES.SERVICES)

    allServiceNames.push(serviceId)

    rows.push({
      id,
      user: `${user}@${remote}`,
      connected: dateFormat({ value: connected }),
      idle,
      memory,
      io_activity,
      serviceId,
    })
  })
  servicesLength.value = [...new Set(allServiceNames)].length
  return rows
})

async function confirmKillSession(id) {
  await sessionsService.kill({ id, callback: sessionsService.fetchSessions })
}
</script>

<template>
  <SessionsTable
    :extraHeaders="[SERVICE_HEADER]"
    :items="items"
    :items-length="total_sessions"
    :hasLoading="false"
    @confirm-kill="confirmKillSession"
    @on-update="sessionsService.fetchSessions"
  >
    <template #[`header.serviceId`]="{ column }">
      {{ column.title }}
      <span class="ml-1 total text-grayed-out"> ({{ servicesLength }}) </span>
    </template>
    <template #[`item.serviceId`]="{ value, highlighter }">
      <CustomCellRenderer
        :value="value"
        :componentName="SERVICE_HEADER.customRender.renderer"
        :objType="SERVICE_HEADER.customRender.objType"
        :highlighter="highlighter"
        v-bind="SERVICE_HEADER.customRender.props"
      />
    </template>
  </SessionsTable>
</template>
