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
  serviceId: { type: String, required: true },
  initialTypeGroups: { type: Object, required: true },
  initialRelationshipItems: { type: Array, required: true },
  data: { type: Array, required: true },
  onSave: { type: Function, required: true },
})

const {
  lodash: { cloneDeep, isEqual },
  arrOfObjsDiff,
} = useHelpers()
const typy = useTypy()

let hasChanged = ref(false)
let selectedItems = ref([])

const defRoutingTarget = computed(() => {
  const types = Object.keys(props.initialTypeGroups)
  const isTargetingCluster = types.includes('monitors')
  const isTargetingServers = types.includes('servers')
  const isTargetingServices = types.includes('services')

  if (isTargetingCluster) return 'cluster'
  else if ((isTargetingServers && isTargetingServices) || isTargetingServices) return 'targets'
  else if (isTargetingServers) return 'servers'
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

  let map = cloneDeep(props.initialTypeGroups)
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
  let relationships = []
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
    :hasChanged="hasChanged"
    :onSave="onClickSave"
  >
    <template v-slot:form-body>
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
