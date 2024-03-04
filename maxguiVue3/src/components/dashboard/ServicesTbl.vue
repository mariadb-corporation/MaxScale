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
import { MXS_OBJ_TYPES, ROUTING_TARGET_RELATIONSHIP_TYPES } from '@/constants'
import OverviewTbl from '@/components/dashboard/OverviewTbl.vue'

const store = useStore()
const { t } = useI18n()
const typy = useTypy()
const { ciStrIncludes } = useHelpers()

const headers = [
  {
    title: 'Service',
    value: 'id',
    autoTruncate: true,
    cellProps: { class: 'px-0', style: { maxWidth: '150px' } },
    customRender: {
      renderer: 'AnchorLink',
      objType: MXS_OBJ_TYPES.SERVICES,
      props: { class: 'px-6' },
    },
  },
  {
    title: 'State',
    value: 'state',
    customRender: { renderer: 'StatusIcon', objType: MXS_OBJ_TYPES.SERVICES },
  },
  { title: 'Router', value: 'router', autoTruncate: true },
  { title: 'Current Sessions', value: 'connections', autoTruncate: true },
  { title: 'Total Sessions', value: 'total_connections', autoTruncate: true },
  {
    title: t('routingTargets'),
    value: 'routingTargets',
    autoTruncate: true,
    cellProps: { class: 'pa-0' },
    customRender: {
      renderer: 'RelationshipItems',
      objType: 'targets',
      mixTypes: true,
      props: { class: 'px-6' },
    },
    filter: (v, query) =>
      ciStrIncludes(typy(v).isArray ? v.map((v) => v.id).join(' ') : String(v), query),
  },
]

let routingTargetsTotal = ref(0)
const all_services = computed(() => store.state.services.all_services)
const totalMap = computed(() => ({ routingTargets: routingTargetsTotal.value }))

const items = computed(() => {
  let rows = []
  let allRoutingTargets = []
  all_services.value.forEach((service) => {
    const {
      id,
      attributes: { state, router, connections, total_connections },
      relationships = {},
    } = service || {}

    const targets = Object.keys(relationships).reduce((arr, type) => {
      if (ROUTING_TARGET_RELATIONSHIP_TYPES.includes(type)) {
        arr = [...arr, ...typy(relationships[type], 'data').safeArray]
      }
      return arr
    }, [])
    const routingTargets = targets.length ? targets : t('noEntity', [t('routingTargets')])

    if (typeof routingTargets !== 'string')
      allRoutingTargets = [...allRoutingTargets, ...routingTargets]
    const row = { id, state, router, connections, total_connections, routingTargets }
    rows.push(row)
  })
  routingTargetsTotal.value = [...new Set(allRoutingTargets.map((target) => target.id))].length
  return rows
})
</script>

<template>
  <OverviewTbl filter-mode="some" :headers="headers" :data="items" :totalMap="totalMap" />
</template>
