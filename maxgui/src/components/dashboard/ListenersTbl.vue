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
import { MXS_OBJ_TYPES } from '@/constants'
import OverviewTbl from '@/components/dashboard/OverviewTbl.vue'

const store = useStore()

const HEADERS = [
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
    value: 'serviceIds',
    autoTruncate: true,
    cellProps: { class: 'pa-0' },
    customRender: {
      renderer: 'RelationshipItems',
      objType: MXS_OBJ_TYPES.SERVICES,
      props: { class: 'px-6' },
    },
  },
]

const allListeners = computed(() => store.state.listeners.all_objs)
const totalMap = computed(() => ({ serviceIds: totalServices.value }))
const items = computed(() => {
  const rows = []
  allListeners.value.forEach((listener) => {
    const {
      id,
      attributes: {
        state,
        parameters: { port, address, socket },
      },
      relationships: { services: { data: associatedServices = [] } = {} },
    } = listener
    const serviceIds = associatedServices.map((item) => item.id)
    const row = { id, port, address, state, serviceIds }
    if (port === null) row.address = socket

    rows.push(row)
  })
  return rows
})

const totalServices = useCountUniqueValues({ data: items, field: 'serviceIds' })
</script>

<template>
  <OverviewTbl :headers="HEADERS" :data="items" :totalMap="totalMap" />
</template>
