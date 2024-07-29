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
import { MXS_OBJ_TYPE_MAP } from '@/constants'

const props = defineProps({
  serviceId: { type: String, required: true },
  initialTypeGroups: { type: Object, required: true },
  initialRelationshipItems: { type: Array, required: true },
  data: { type: Array, required: true },
  onSave: { type: Function, required: true },
})

const { SERVERS, SERVICES, MONITORS } = MXS_OBJ_TYPE_MAP
const {
  lodash: { cloneDeep, isEqual },
  arrOfObjsDiff,
} = useHelpers()
const typy = useTypy()

const hasChanged = ref(false)
const selectedItems = ref([])

const defRoutingTarget = computed(() => {
  const types = Object.keys(props.initialTypeGroups)
  const isTargetingCluster = types.includes(MONITORS)
  const isTargetingServers = types.includes(SERVERS)
  const isTargetingServices = types.includes(SERVICES)

  if (isTargetingCluster) return 'cluster'
  else if ((isTargetingServers && isTargetingServices) || isTargetingServices) return 'targets'
  else if (isTargetingServers) return SERVERS
  return ''
})
const initialValue = computed(() => {
  const initialItems = cloneDeep(Object.values(props.initialTypeGroups)).flat()
  return defRoutingTarget.value === 'cluster'
    ? typy(initialItems, '[0]').safeObjectOrEmpty
    : initialItems
})

function getNewRoutingTargetMap() {
  const diff = arrOfObjsDiff({
    base: props.initialRelationshipItems,
    newArr: selectedItems.value,
    idField: 'id',
  })
  const removedObjs = diff.get('removed')
  const addedObjs = diff.get('added')

  const map = cloneDeep(props.initialTypeGroups)
  addedObjs.forEach((obj) => {
    const type = obj.type
    if (map[type]) map[type].push(obj)
    else map[type] = [obj]
  })
  removedObjs.forEach((obj) => {
    const type = obj.type
    if (map[type]) map[type] = map[type].filter((item) => item.id !== obj.id)
  })

  return map
}

async function onClickSave() {
  const map = getNewRoutingTargetMap()
  const relationships = []
  for (const type of Object.keys(map)) {
    const newData = map[type]
    if (!isEqual(props.initialTypeGroups[type], newData))
      relationships.push({ type, data: newData })
  }
  await props.onSave(relationships)
}
</script>

<template>
  <BaseDlg
    :title="$t('editEntity', { entityName: $t('routingTargets', 2) })"
    saveText="save"
    :saveDisabled="!hasChanged"
    :onSave="onClickSave"
  >
    <template #form-body>
      <RoutingTargetSelect
        v-model="selectedItems"
        :serviceId="serviceId"
        :defRoutingTarget="defRoutingTarget"
        :initialValue="initialValue"
        @has-changed="hasChanged = $event"
      />
    </template>
  </BaseDlg>
</template>
