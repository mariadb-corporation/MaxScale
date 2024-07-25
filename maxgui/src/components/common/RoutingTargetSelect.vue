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

const props = defineProps({
  modelValue: { type: [Array, Object], required: true },
  initialValue: { type: [Array, Object], default: () => [] },
  defRoutingTarget: { type: String, default: 'servers' },
  serviceId: { type: String, default: '' }, // the id of the service object being altered
})
const emit = defineEmits(['update:modelValue', 'has-changed'])

const { SERVERS, SERVICES, MONITORS } = MXS_OBJ_TYPES
const { t } = useI18n()
const typy = useTypy()
const fetchObjData = useFetchObjData()
const routingTargets = [
  { txt: 'servers and services', value: 'targets' },
  { txt: 'servers', value: SERVERS },
  { txt: 'cluster', value: 'cluster' },
]

const allTargetsMap = ref({})
const itemsList = ref([])
const chosenTarget = ref('')

const selectedItems = computed({
  get() {
    return props.modelValue
  },
  set(v) {
    emit('update:modelValue', v)
  },
})
const allowMultiple = computed(() => chosenTarget.value !== 'cluster')
const type = computed(() => {
  switch (chosenTarget.value) {
    case 'targets':
      return 'items'
    case 'cluster':
      return 'clusters'
    default:
      return chosenTarget.value || 'items'
  }
})
const specifyRoutingTargetsLabel = computed(() => {
  switch (chosenTarget.value) {
    case 'targets':
    case 'servers':
      return `${t('specify', 2)} ${t(type.value, 2)}`
    default:
      return `${t('specify', 1)} ${t(type.value, 1)}`
  }
})
const chosenRelationshipTypes = computed(() => {
  switch (chosenTarget.value) {
    case 'targets':
      return [SERVICES, SERVERS]
    case 'cluster':
      return [MONITORS]
    case SERVERS:
      return [SERVERS]
    default:
      return []
  }
})

watch(chosenTarget, () => {
  assignItemList()
})
watch(
  () => props.defRoutingTarget,
  (v) => {
    if (v) chosenTarget.value = v
  },
  { immediate: true }
)

onBeforeMount(async () => {
  allTargetsMap.value = await getAllTargetsMap()
  assignItemList()
})

async function getAllTargetsMap() {
  const map = {}
  const relationshipTypes = [SERVICES, SERVERS, MONITORS]
  for (const type of relationshipTypes) {
    const data = await fetchObjData({ type, fields: ['id'] })
    if (!map[type]) map[type] = []
    map[type] = [
      ...map[type],
      ...data.reduce((arr, item) => {
        // cannot target the service itself
        if (item.id !== props.serviceId) arr.push({ id: item.id, type: item.type })
        return arr
      }, []),
    ]
  }
  return map
}

function assignItemList() {
  itemsList.value = chosenRelationshipTypes.value.reduce((arr, type) => {
    arr.push(...typy(allTargetsMap.value, `${[type]}`).safeArray)
    return arr
  }, [])
}

function onChangeRoutingTarget() {
  selectedItems.value = selectedItems.value.filter((item) =>
    chosenRelationshipTypes.value.includes(item.type)
  )
}
</script>

<template>
  <div>
    <label class="label-field text-small-text d-block" for="routingTargetOpts">
      {{ $t('selectRoutingTargets') }}
    </label>
    <VSelect
      v-model="chosenTarget"
      id="routingTargetOpts"
      :items="routingTargets"
      item-title="txt"
      item-value="value"
      @update:modelValue="onChangeRoutingTarget"
    />
    <label class="label-field text-small-text d-block" for="routingTargets">
      {{ specifyRoutingTargetsLabel }}
    </label>
    <ObjSelect
      v-model="selectedItems"
      id="routingTargets"
      :items="itemsList"
      :type="type"
      :multiple="allowMultiple"
      :initialValue="initialValue"
      :showPlaceHolder="false"
      v-bind="$attrs"
      @has-changed="emit('has-changed', $event)"
    />
  </div>
</template>
