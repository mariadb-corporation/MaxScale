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
const { t } = useI18n()

const headers = [
  {
    title: 'Filter',
    value: 'id',
    autoTruncate: true,
    cellProps: { class: 'pa-0' },
    customRender: {
      renderer: 'AnchorLink',
      objType: MXS_OBJ_TYPES.FILTERS,
      props: { class: 'px-6' },
    },
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
  { title: 'Module', value: 'module' },
]

let totalServices = ref(0)

const all_filters = computed(() => store.state.filters.all_filters)
const totalMap = computed(() => ({ serviceIds: totalServices.value }))

const items = computed(() => {
  let rows = []
  let allServiceIds = []
  all_filters.value.forEach((filter) => {
    const {
      id,
      attributes: { module } = {},
      relationships: { services: { data: associatedServices = [] } = {} },
    } = filter || {}

    const serviceIds = associatedServices.length
      ? associatedServices.map((item) => item.id)
      : t('noEntity', { entityName: 'services' })

    if (typy(serviceIds).isArray) allServiceIds.push(serviceIds)

    rows.push({ id, serviceIds, module })
  })

  totalServices.value = [...new Set(allServiceIds)].length
  return rows
})
</script>

<template>
  <OverviewTbl :headers="headers" :data="items" :totalMap="totalMap" />
</template>
