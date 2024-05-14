<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import TblToolbar from '@wsComps/DdlEditor/TblToolbar.vue'
import IndexList from '@wsComps/DdlEditor/IndexList.vue'
import IndexColList from '@wsComps/DdlEditor/IndexColList.vue'
import { CREATE_TBL_TOKENS, KEY_EDITOR_ATTRS, KEY_EDITOR_ATTR_IDX_MAP } from '@/constants/workspace'

const props = defineProps({
  modelValue: { type: Object, required: true },
  dim: { type: Object, required: true },
  tableColNameMap: { type: Object, required: true },
  tableColMap: { type: Object, required: true },
})
const emit = defineEmits(['update:modelValue'])

const IDX_OF_ID = KEY_EDITOR_ATTR_IDX_MAP[KEY_EDITOR_ATTRS.ID]
const IDX_OF_CATEGORY = KEY_EDITOR_ATTR_IDX_MAP[KEY_EDITOR_ATTRS.CATEGORY]
const {
  lodash: { isEqual, cloneDeep },
  immutableUpdate,
  uuidv1,
} = useHelpers()
const typy = useTypy()
const indexListRef = ref(null)
const headerHeight = ref(0)
const selectedItems = ref([])
const stagingKeyCategoryMap = ref({})

const keyTblWidth = computed(() => props.dim.width / 2.25)
const keyColsTblWidth = computed(() => props.dim.width - keyTblWidth.value - 16)
const keyCategoryMap = computed({
  get: () => props.modelValue,
  set: (v) => emit('update:modelValue', v),
})
const selectedItem = computed(() => typy(selectedItems.value, '[0]').safeArray)
const selectedKeyId = computed(() => selectedItem.value[IDX_OF_ID])
const selectedKeyCategory = computed(() => selectedItem.value[IDX_OF_CATEGORY])

watch(
  keyCategoryMap,
  (v) => {
    if (!isEqual(v, stagingKeyCategoryMap.value)) {
      init()
      selectFirstItem()
    }
  },
  { deep: true }
)
watch(
  stagingKeyCategoryMap,
  (v) => {
    if (!isEqual(keyCategoryMap.value, v)) keyCategoryMap.value = v
  },
  { deep: true }
)

onBeforeMount(() => init())

function init() {
  stagingKeyCategoryMap.value = cloneDeep(keyCategoryMap.value)
}

function selectFirstItem() {
  selectedItems.value = []
  nextTick(() => indexListRef.value.handleSelectItem(0))
}

function deleteSelectedKeys() {
  const item = selectedItem.value
  const id = item[IDX_OF_ID]
  const category = item[IDX_OF_CATEGORY]
  let keyMap = stagingKeyCategoryMap.value[category] || {}
  keyMap = immutableUpdate(keyMap, {
    $unset: [id],
  })
  stagingKeyCategoryMap.value = immutableUpdate(
    stagingKeyCategoryMap.value,
    Object.keys(keyMap).length ? { [category]: { $set: keyMap } } : { $unset: [category] }
  )
  selectFirstItem()
}

function addNewKey() {
  const plainKey = CREATE_TBL_TOKENS.key
  const currPlainKeyMap = stagingKeyCategoryMap.value[plainKey] || {}
  const newKey = {
    id: `key_${uuidv1()}`,
    cols: [],
    name: `index_${Object.keys(currPlainKeyMap).length}`,
  }
  stagingKeyCategoryMap.value = immutableUpdate(stagingKeyCategoryMap.value, {
    $merge: {
      [plainKey]: { ...currPlainKeyMap, [newKey.id]: newKey },
    },
  })
  // wait for the next tick to ensure the list is regenerated before selecting the item
  nextTick(
    () => indexListRef.value.handleSelectItem(-1) // select last item
  )
}
</script>

<template>
  <div class="fill-height">
    <TblToolbar
      :selectedItems="selectedItems"
      :showRotateTable="false"
      reverse
      @get-computed-height="headerHeight = $event"
      @on-delete="deleteSelectedKeys"
      @on-add="addNewKey"
    />
    <div class="d-flex flex-row">
      <IndexList
        ref="indexListRef"
        v-model="stagingKeyCategoryMap"
        v-model:selectedItems="selectedItems"
        :dim="{ height: dim.height - headerHeight, width: keyTblWidth }"
        class="mr-4"
      />
      <IndexColList
        v-if="selectedKeyId"
        v-model="stagingKeyCategoryMap"
        :dim="{ height: dim.height - headerHeight, width: keyColsTblWidth }"
        :keyId="selectedKeyId"
        :category="selectedKeyCategory"
        :tableColNameMap="tableColNameMap"
        :tableColMap="tableColMap"
      />
    </div>
  </div>
</template>
