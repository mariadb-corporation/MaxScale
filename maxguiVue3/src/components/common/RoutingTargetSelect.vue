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
  modelValue: { type: [Array, Object], required: true },
  initialValue: { type: [Array, Object], default: () => [] },
  defRoutingTarget: { type: String, default: 'servers' },
  serviceId: { type: String, default: '' }, // the id of the service object being altered
})
const emit = defineEmits(['update:modelValue', 'has-changed'])

const { t } = useI18n()
const typy = useTypy()
const fetchObjData = useFetchObjData()
const routingTargets = [
  { txt: 'servers and services', value: 'targets' },
  { txt: 'servers', value: 'servers' },
  { txt: 'cluster', value: 'cluster' },
]

let allTargetsMap = ref({})
let itemsList = ref([])
let chosenTarget = ref('')

let selectedItems = computed({
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
  let types = []
  switch (chosenTarget.value) {
    case 'targets':
      types = ['services', 'servers']
      break
    case 'cluster':
      types = ['monitors']
      break
    case 'servers':
      types = ['servers']
      break
  }
  return types
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
  let map = {}
  let relationshipTypes = ['services', 'servers', 'monitors']
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
    <label class="field__label text-small-text d-block" for="routingTargetOpts">
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
    <label class="field__label text-small-text d-block" for="routingTargets">
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
