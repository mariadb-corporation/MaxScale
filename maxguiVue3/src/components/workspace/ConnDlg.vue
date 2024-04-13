<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Worksheet from '@wsModels/Worksheet'
import queryConnService from '@/services/queryConnService'
import { MXS_OBJ_TYPES } from '@/constants'

const { LISTENERS, SERVERS, SERVICES } = MXS_OBJ_TYPES
const itemTypes = [LISTENERS, SERVERS, SERVICES]

const attrs = useAttrs()
const store = useStore()

const fetchAll = useFetchObjData()

let selectedItem = ref(null)
let payload = ref({
  user: '',
  password: '',
  db: '',
  timeout: 300,
})
let objItemsMap = ref({})

const def_conn_obj_type = computed(() => store.state.prefAndStorage.def_conn_obj_type)
const reqConfig = computed(() => Worksheet.getters('activeRequestConfig'))
const conn_err_state = computed(() => store.state.queryConnsMem.conn_err_state)
const pre_select_conn_item = computed(() => store.state.queryConnsMem.pre_select_conn_item)
let selectedType = computed({
  get: () =>
    pre_select_conn_item.value ? pre_select_conn_item.value.type : def_conn_obj_type.value,
  set: (v) => store.commit('prefAndStorage/SET_DEF_CONN_OBJ_TYPE', v),
})
const items = computed(() => objItemsMap.value[selectedType.value] || [])
const hasSavingErr = computed(() => conn_err_state.value)

watch(
  () => attrs.modelValue,
  async (v) => {
    if (v) {
      await onChangeType(selectedType.value)
    } else resetFormData()
  },
  { immediate: true }
)
watch(selectedType, async (v) => {
  if (v) await onChangeType(v)
})

async function onChangeType(v) {
  if (!objItemsMap.value[v]) await fetchItems(v)
  handleSelectDefItem(v)
}

async function fetchItems(type) {
  let items = await fetchAll({ type, fields: ['id'], reqConfig: reqConfig.value })
  objItemsMap.value[type] = items.map(({ id, type }) => ({
    id,
    type,
  }))
}

function resetFormData() {
  payload.value.timeout = 300
  objItemsMap.value = {}
  store.commit('queryConnsMem/SET_PRE_SELECT_CONN_ITEM', null)
}

function handleSelectDefItem() {
  if (items.value.length) {
    if (pre_select_conn_item.value) selectedItem.value = pre_select_conn_item.value
    else selectedItem.value = items.value[0]
  }
}

async function confirmOpen() {
  const { id } = selectedItem.value
  await queryConnService.handleOpenConn({
    body: { target: id, ...payload.value },
    meta: { name: id },
  })
}
</script>

<template>
  <BaseDlg
    :onSave="confirmOpen"
    :title="`${$t('connectTo')}...`"
    minBodyWidth="512px"
    :hasSavingErr="hasSavingErr"
    hasFormDivider
    :disableOnSaveError="false"
  >
    <template #body>
      <ObjTypeSelect v-model="selectedType" :items="itemTypes" class="mt-4" />
    </template>
    <template #form-body>
      <VContainer class="pa-1">
        <VRow class="my-0 mx-n1">
          <VCol v-if="selectedType" cols="12" md="12" class="pa-1">
            <label class="label-field text-small-text label--required" for="obj-select">
              {{ $t('mxsObjLabelName', { type: $t(selectedType, 1) }) }}
            </label>
            <ObjSelect
              v-model="selectedItem"
              id="obj-select"
              :items="items"
              :type="selectedType"
              showPlaceHolder
              required
              hide-details="auto"
            />
          </VCol>
          <VCol cols="12" md="6" class="pa-1">
            <UidInput v-model="payload.user" name="db-user" />
          </VCol>
          <VCol cols="12" md="6" class="pa-1">
            <PwdInput v-model="payload.password" name="db-password" />
          </VCol>
          <VCol cols="12" md="6" class="pa-1">
            <LabelField v-model="payload.db" :label="$t('database')" name="db" />
          </VCol>
          <VCol cols="12" md="6" class="pa-1">
            <TimeoutInput v-model="payload.timeout" name="timeout" />
          </VCol>
        </VRow>
      </VContainer>
    </template>
  </BaseDlg>
</template>
