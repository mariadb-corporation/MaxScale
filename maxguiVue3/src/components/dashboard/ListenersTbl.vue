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
import OverviewTbl from '@/components/dashboard/OverviewTbl.vue'

const store = useStore()
const typy = useTypy()

const headers = [
  {
    title: 'Listener',
    value: 'id',
    autoTruncate: true,
    cellProps: { class: 'pa-0' },
    customRender: {
      renderer: 'AnchorLink',
      objType: MXS_OBJ_TYPES.LISTENERS,
      props: { class: 'px-6' },
    },
  },
  { title: 'Port', value: 'port' },
  { title: 'Host', value: 'address' },
  {
    title: 'State',
    value: 'state',
    customRender: { renderer: 'StatusIcon', objType: MXS_OBJ_TYPES.LISTENERS },
  },
  {
    title: 'Service',
    value: 'serviceId',
    autoTruncate: true,
    cellProps: { class: 'pa-0' },
    customRender: {
      renderer: 'AnchorLink',
      objType: MXS_OBJ_TYPES.SERVICES,
      props: { class: 'px-6' },
    },
  },
]

let totalServices = ref(0)

const all_listeners = computed(() => store.state.listeners.all_listeners)
const totalMap = computed(() => ({ serviceId: totalServices.value }))

const items = computed(() => {
  let rows = []
  let allServiceIds = []
  all_listeners.value.forEach((listener) => {
    const {
      id,
      attributes: {
        state,
        parameters: { port, address, socket },
      },
      relationships: { services: { data: associatedServices = [] } = {} },
    } = listener

    // always has one service
    const serviceId = typy(associatedServices[0], 'id').safeString

    allServiceIds.push(serviceId)

    let row = { id, port, address, state, serviceId }

    if (port === null) row.address = socket

    rows.push(row)
  })

  totalServices.value = [...new Set(allServiceIds)].length
  return rows
})
</script>

<template>
  <OverviewTbl :headers="headers" :data="items" :totalMap="totalMap" />
</template>
