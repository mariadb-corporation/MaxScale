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
const { t } = useI18n()

const HEADERS = [
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

const allFilters = computed(() => store.state.filters.all_objs)
const totalMap = computed(() => ({ serviceIds: totalServices.value }))

const items = computed(() => {
  const rows = []
  allFilters.value.forEach((filter) => {
    const {
      id,
      attributes: { module } = {},
      relationships: { services: { data: associatedServices = [] } = {} },
    } = filter || {}

    const serviceIds = associatedServices.length
      ? associatedServices.map((item) => item.id)
      : t('noEntity', [MXS_OBJ_TYPES.SERVICES])
    rows.push({ id, serviceIds, module })
  })
  return rows
})

const totalServices = useCountUniqueValues({ data: items, field: 'serviceIds' })
</script>

<template>
  <OverviewTbl :headers="HEADERS" :data="items" :totalMap="totalMap" />
</template>
