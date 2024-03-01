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

const props = defineProps({
  defFormType: { type: String, default: '' },
  defRelationshipObj: { type: Object, default: () => {} },
})

const store = useStore()
const typy = useTypy()
const { t } = useI18n()
const fetchAllObjIds = useFetchAllObjIds()

const form_type = computed(() => store.state.form_type)
const all_filters = computed(() => store.state.filters.all_filters)
const all_modules_map = computed(() => store.state.maxscale.all_modules_map)
const all_monitors = computed(() => store.state.monitors.all_monitors)
const all_servers = computed(() => store.state.servers.all_servers)
const all_services = computed(() => store.state.services.all_services)
const isAdmin = computed(() => store.getters['users/isAdmin'])
const getMxsObjModules = computed(() => store.getters['maxscale/getMxsObjModules'])

const defRelationshipObjType = computed(() => typy(props.defRelationshipObj, 'type').safeString)
const moduleParamsProps = computed(() => {
  return {
    modules: getMxsObjModules.value(selectedObjType.value),
    validate: typy(dialogRef.value, 'validateForm').safeFunction,
  }
})

let allObjIds = ref([])
let dialogRef = ref(null)
let formRef = ref(null)
let isDlgOpened = ref(false)
let selectedObjType = ref('')
let objId = ref('')
let defRelationshipItems = ref([])
let serviceDefRoutingTargetItems = ref([])
let serviceDefFilterItems = ref([])

watch(form_type, async (v) => {
  // trigger open dialog since form_type is used to open dialog without clicking button in this component
  if (v) await onCreate()
})
watch(isDlgOpened, async (v) => {
  if (v) handleSetFormType()
  else if (form_type.value) store.commit('SET_FORM_TYPE', null) // clear form_type
})
watch(selectedObjType, async (v) => {
  await onChangeObjType(v)
})
watch(objId, async (v) => {
  // add hyphens when ever input have whitespace
  objId.value = v ? v.split(' ').join('-') : v
})

async function onCreate() {
  // fetch data before open dlg
  if (typy(all_modules_map.value).isEmptyObject) {
    await store.dispatch('maxscale/fetchAllModules')
    allObjIds.value = await fetchAllObjIds()
  }
  isDlgOpened.value = true
}

/**
 *  global form_type state has higher priority. It is
 *  used to trigger opening form dialog without
 *  clicking the button in this component
 */
function handleSetFormType() {
  if (form_type.value) selectedObjType.value = form_type.value
  else if (props.defFormType) selectedObjType.value = props.defFormType
  else selectedObjType.value = MXS_OBJ_TYPES.SERVICES
}

/**
 * If current page is a detail page and have relationship object,
 * set default relationship items
 */
async function onChangeObjType(val) {
  const { SERVICES, SERVERS, MONITORS, LISTENERS, FILTERS } = MXS_OBJ_TYPES
  switch (val) {
    case SERVICES:
      if (defRelationshipObjType.value === SERVERS)
        serviceDefRoutingTargetItems.value = [props.defRelationshipObj]
      else if (defRelationshipObjType.value === FILTERS)
        serviceDefFilterItems.value = [props.defRelationshipObj]
      break
    case SERVERS:
      if (defRelationshipObjType.value === SERVICES)
        defRelationshipItems.value = [props.defRelationshipObj]
      else if (defRelationshipObjType.value === MONITORS)
        defRelationshipItems.value = props.defRelationshipObj
      break
    case MONITORS:
      if (defRelationshipObjType.value === SERVERS)
        defRelationshipItems.value = [props.defRelationshipObj]
      break
    case LISTENERS: {
      if (defRelationshipObjType.value === SERVICES)
        defRelationshipItems.value = props.defRelationshipObj
      break
    }
  }
}

async function onSave() {
  const type = selectedObjType.value
  const { moduleId, parameters, relationships } = formRef.value.getValues()
  const { SERVICES, SERVERS, MONITORS, LISTENERS, FILTERS } = MXS_OBJ_TYPES
  let payload = {
    id: objId.value,
    parameters,
    callback: async () => await store.dispatch(`${type}/fetchAll`),
  }
  switch (type) {
    case SERVICES:
    case MONITORS:
      payload.module = moduleId
      payload.relationships = relationships

      break
    case LISTENERS:
    case SERVERS:
      payload.relationships = relationships
      break
    case FILTERS:
      payload.module = moduleId
      break
  }
  await store.dispatch(`${type}/create`, payload)
  reloadHandler()
}

function reloadHandler() {
  if (defRelationshipItems.value) store.commit('SET_SHOULD_REFRESH_RESOURCE', true)
}

function validateResourceId(val) {
  if (!val) return t('errors.requiredInput', { inputName: 'id' })
  else if (allObjIds.value.includes(val)) return t('errors.duplicatedValue')
  return true
}
</script>

<template>
  <div v-if="isAdmin">
    <VBtn
      :width="160"
      size="small"
      :height="36"
      color="primary"
      rounded
      variant="outlined"
      class="text-capitalize px-8 font-weight-medium"
      data-test="cancel-btn"
      @click="onCreate"
    >
      + {{ $t('createNew') }}
    </VBtn>
    <BaseDlg
      ref="dialogRef"
      v-model="isDlgOpened"
      :onSave="onSave"
      :title="`${$t('createANew')}...`"
      hasFormDivider
      minBodyWidth="635px"
    >
      <template #body>
        <VSelect
          v-model="selectedObjType"
          :items="Object.values(MXS_OBJ_TYPES)"
          class="mt-4"
          hide-details
          :rules="[(v) => !!v || $t('errors.requiredInput', { inputName: 'This field' })]"
          required
          @update:modelValue="onChangeObjType"
        >
          <template #item="{ props }">
            <VListItem v-bind="props">
              <template #title="{ title }">
                <span v-if="title" class="text-capitalize"> {{ $t(title, 1) }}</span>
              </template>
            </VListItem>
          </template>
          <template #selection="{ item }">
            <span v-if="item.title" class="font-weight-bold text-capitalize">{{
              $t(item.title, 1)
            }}</span>
          </template>
        </VSelect>
      </template>
      <template v-if="selectedObjType" #form-body>
        <label class="field__label text-small-text d-block" for="obj-id">
          {{ $t('mxsObjLabelName', { type: $t(selectedObjType, 1) }) }}
        </label>
        <VTextField
          v-model="objId"
          :rules="[(v) => validateResourceId(v)]"
          name="id"
          required
          :placeholder="$t('nameYour', { type: $t(selectedObjType, 1).toLowerCase() })"
          hide-details="auto"
          class="mb-3"
          id="obj-id"
        />
        <ServiceForm
          v-if="selectedObjType === MXS_OBJ_TYPES.SERVICES"
          ref="formRef"
          :allFilters="all_filters"
          :defRoutingTargetItems="serviceDefRoutingTargetItems"
          :defFilterItem="serviceDefFilterItems"
          :moduleParamsProps="moduleParamsProps"
        />
        <MonitorForm
          v-else-if="selectedObjType === MXS_OBJ_TYPES.MONITORS"
          ref="formRef"
          :allServers="all_servers"
          :defaultItems="defRelationshipItems"
          :moduleParamsProps="moduleParamsProps"
        />
        <FilterForm
          v-else-if="selectedObjType === MXS_OBJ_TYPES.FILTERS"
          ref="formRef"
          :moduleParamsProps="moduleParamsProps"
        />
        <ListenerForm
          v-else-if="selectedObjType === MXS_OBJ_TYPES.LISTENERS"
          ref="formRef"
          :allServices="all_services"
          :defaultItems="defRelationshipItems"
          :moduleParamsProps="moduleParamsProps"
        />
        <ServerForm
          v-else-if="selectedObjType === MXS_OBJ_TYPES.SERVERS"
          ref="formRef"
          :allServices="all_services"
          :allMonitors="all_monitors"
          :defaultItems="defRelationshipItems"
          :moduleParamsProps="moduleParamsProps"
          class="mt-4"
        />
      </template>
    </BaseDlg>
  </div>
</template>
