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
import { MXS_OBJ_TYPES } from '@/constants'

const store = useStore()
const { t } = useI18n()
const typy = useTypy()
const { dateFormat } = useHelpers()
let servicesLength = ref(0)

const serviceHeader = {
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
const totalSessions = computed(() => store.getters['sessions/total'])
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
      : t('noEntity', { entityName: 'services' })

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

async function killSession(id) {
  await store.dispatch('sessions/killSession', { id, callback: fetchSessions })
}

async function fetchSessions() {
  await store.dispatch('sessions/fetchAll')
}
</script>

<template>
  <SessionsTable
    :extraHeaders="[serviceHeader]"
    :items="items"
    :items-length="totalSessions"
    :hasLoading="false"
    @confirm-kill="killSession"
    @on-update="fetchSessions"
  >
    <template #[`header.serviceId`]="{ column }">
      {{ column.title }}
      <span class="ml-1 total text-grayed-out"> ({{ servicesLength }}) </span>
    </template>
    <template #[`item.serviceId`]="{ value, highlighter }">
      <CustomCellRenderer
        :value="value"
        :componentName="serviceHeader.customRender.renderer"
        :objType="serviceHeader.customRender.objType"
        :highlighter="highlighter"
        v-bind="serviceHeader.customRender.props"
      />
    </template>
  </SessionsTable>
</template>
