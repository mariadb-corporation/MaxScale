<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import ParameterInput from '@/components/common/ParametersTable/ParameterInput.vue'
import { MXS_OBJ_TYPES } from '@/constants'
import { treeToObj } from '@/utils/treeTableHelpers'
import { isServerOrListenerType } from '@/components/common/ParametersTable/utils'
import { VForm } from 'vuetify/lib/components/index.mjs'

const props = defineProps({
  data: { type: Object, required: true },
  paramsInfo: { type: Array, required: true },
  mxsObjType: { type: String, default: '' }, // MXS_OBJ_TYPES
  creationMode: { type: Boolean, default: false },
  confirmEdit: { type: Function, default: () => null },
  showAdvanceToggle: { type: Boolean, default: false },
  titleWrapperClass: { type: String, default: '' },
  tableProps: { type: Object, default: () => ({}) },
  /**
   * If a custom form validation function is defined,
   * it will use this function to validate inputs, VForm
   * component will not be rendered in this component.
   */
  parentValidate: { type: Function },
})

const emit = defineEmits(['changed-params'])

const {
  lodash: { keyBy, isEqual, merge, omit },
} = useHelpers()
const typy = useTypy()

const store = useStore()
const isAdmin = computed(() => store.getters['users/isAdmin'])
const search_keyword = computed(() => store.state.search_keyword)
const loading = useLoading()

const treeTableProps = computed(() =>
  merge(
    {
      expandAll: true,
      showCellBorder: true,
      showKeyLength: true,
      arrayTransform: false,
      loading: props.creationMode ? false : loading.value,
    },
    props.tableProps
  )
)
const hasParentForm = computed(() => typy(props.parentValidate).isDefined)

let port = ref(null)
let socket = ref(null)
let showEditBtn = ref(false)
let isEditing = ref(false)
let isAdvanced = ref(false)
let showConfirmDialog = ref(false)
let nodes = ref([])
let isFormValid = ref(false)
let form = ref(null)
let changedNodeMap = ref({})
let paramsObj = ref({})
/**
 * These params for `servers` and `listeners` are not mandatory from
 * the API perspective but it should be always shown to the users, so
 * that they can either define socket or address and port.
 */
const SPECIAL_PARAMS = ['address', 'port', 'socket']

const isObjWithSpecialParams = computed(() => isServerOrListenerType(props.mxsObjType))
const isServerType = computed(() => props.mxsObjType === MXS_OBJ_TYPES.SERVERS)
const isListenerType = computed(() => props.mxsObjType === MXS_OBJ_TYPES.LISTENERS)

const paramInfoMap = computed(() => {
  let data = props.paramsInfo
  // Show only mandatory params
  if (props.showAdvanceToggle && !isAdvanced.value) {
    data = props.paramsInfo.map((param) => {
      let item = { ...param, hidden: !param.mandatory }
      if (isObjWithSpecialParams.value && SPECIAL_PARAMS.includes(param.name)) item.hidden = false
      return item
    })
  }
  return keyBy(data, 'name')
})

const nodeMap = computed(() => keyBy(nodes.value, 'id'))

const isListener = computed(() => props.mxsObjType === MXS_OBJ_TYPES.LISTENERS)
const changedNodeIds = computed(() => Object.keys(changedNodeMap.value))
const hasChanged = computed(() => {
  if (changedNodeIds.value.length > 0 && isFormValid.value) return true
  return false
})
const changedParams = computed(() =>
  treeToObj({
    changedNodes: Object.values(changedNodeMap.value),
    nodeMap: nodeMap.value,
  })
)

watchEffect(() => {
  // Pause the effect if editing mode is on to prevent unwanted update to the table.
  if (!isEditing.value) {
    let data = props.data
    // server and listener param has a "type" parameter which is not modifiable and not necessary to show
    if (isServerType.value || isListenerType.value) data = omit(data, ['type'])
    paramsObj.value = data
  }
})

watch(
  nodes,
  (v) => {
    if (isObjWithSpecialParams.value) v.forEach((node) => setPortAndSocketValues(node))
  },
  { immediate: true }
)

watch(
  changedParams,
  (v) => {
    emit('changed-params', v)
  },
  { immediate: true, deep: true }
)

/**
 * Assign value to component's data: port, socket
 * @param {Object} param
 */
function setPortAndSocketValues(param) {
  const { key, value } = param
  if (key === 'port') port.value = value
  else if (key === 'socket') socket.value = value
}

function mouseHandler(e) {
  if (isAdmin.value)
    if (e.type === 'mouseenter') showEditBtn.value = true
    else if (e.type === 'mouseleave') showEditBtn.value = false
}

async function onChangeParam({ id, key, value }) {
  const node = nodeMap.value[id]
  if (isEqual(node.value, value)) delete changedNodeMap.value[id]
  else changedNodeMap.value[id] = { ...node, value }
  setPortAndSocketValues({ key, value })
}

/**
 *
 * @param {Object} obj - nested object
 * @param {String} prefix - prefix to add to each properties
 * @returns {String} e.g. rootProp.childProp.grandChildProp
 */
function keyify(obj, prefix = '') {
  return Object.keys(obj).reduce((res, el) => {
    if (typeof obj[el] === 'object' && obj[el] !== null) {
      return [...res, ...keyify(obj[el], prefix + el + '.')]
    }
    return [...res, prefix + el]
  }, [])
}
/**
 * If a node changes its value, its ancestor needs to be included as well
 * this gets it ancestor obj then calls keyify to
 * get key name. e.g. rootProp.childProp.grandChildProp
 * @param {Object} node - parameter node
 * @return {String} the key name of its ancestor. e.g. rootProp.childProp.grandChildProp
 */
function keyifyChangedParams(node) {
  const allKeys = keyify(changedParams.value)
  let result = ''
  for (const key of allKeys) {
    if (key.includes(node.key)) {
      result = key
      break
    }
  }
  return result
}

function cleanup() {
  isEditing.value = false
  changedNodeMap.value = {}
}

async function confirm() {
  await props.confirmEdit(changedParams.value)
  cleanup()
}
async function validate() {
  if (hasParentForm.value) await typy(props, 'parentValidate').safeFunction()
  else await typy(form.value, 'validate').safeFunction()
}
</script>
<template>
  <CollapsibleCtr
    :title="`${$t('parameters', 2)}`"
    :titleWrapperClass="titleWrapperClass"
    @mouseenter="mouseHandler"
    @mouseleave="mouseHandler"
  >
    <template v-if="!creationMode" #title-append>
      <VFadeTransition>
        <VBtn
          v-if="showEditBtn || isEditing"
          density="comfortable"
          variant="text"
          icon
          data-test="edit-btn"
          @click="isEditing = true"
        >
          <vIcon color="primary" size="18" icon="mxs:edit" />
        </VBtn>
      </VFadeTransition>
    </template>
    <template #header-right>
      <VFadeTransition>
        <VSwitch
          v-if="creationMode || showAdvanceToggle"
          v-model="isAdvanced"
          :label="$t('advanced')"
          class="mt-0 pt-3 mr-2"
          hide-details
          color="primary"
        />
        <VBtn
          v-if="isEditing && !creationMode"
          color="primary"
          rounded
          size="small"
          class="text-capitalize"
          data-test="done-editing-btn"
          @click="() => (showConfirmDialog = true)"
        >
          {{ $t('doneEditing') }}
        </VBtn>
      </VFadeTransition>
    </template>
    <component :is="hasParentForm ? 'div' : VForm" ref="form" v-model="isFormValid">
      <TreeTable
        :data="paramsObj"
        :search="search_keyword"
        :keyInfoMap="paramInfoMap"
        v-bind="treeTableProps"
        @get-nodes="nodes = $event"
      >
        <template v-if="isEditing || creationMode" #[`item.value`]="{ item }">
          <ParameterInput
            v-if="item.leaf"
            class="py-1 px-2"
            :item="item"
            :keyInfo="$typy(paramInfoMap[item.key]).safeObjectOrEmpty"
            :creationMode="creationMode"
            :isListener="isListener"
            :portValue="port"
            :socketValue="socket"
            @on-change="onChangeParam"
            @blur="validate()"
          />
        </template>
      </TreeTable>
    </component>
    <BaseDlg
      v-model="showConfirmDialog"
      :onSave="confirm"
      :title="$t('implementChanges')"
      saveText="confirm"
      :hasChanged="hasChanged"
      @after-cancel="cleanup"
    >
      <template #form-body>
        <span class="d-block confirmation-text mb-4">
          {{ $t('changeTheFollowingParameter', { n: changedNodeIds.length }) }}
        </span>
        <div v-for="(node, id) in changedNodeMap" :key="id" class="d-block changed-parameter">
          <p class="d-block mt-2 mb-4">
            <span class="font-weight-bold">{{
              node.parentId ? keyifyChangedParams(node) : node.key
            }}</span>
            <span v-if="$typy(paramInfoMap[node.key], 'type').safeString !== 'password'">
              : {{ node.value }}
            </span>
          </p>
        </div>
      </template>
    </BaseDlg>
  </CollapsibleCtr>
</template>
