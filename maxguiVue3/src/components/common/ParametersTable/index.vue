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

const props = defineProps({
  data: { type: Object, required: true },
  paramsInfo: { type: Array, required: true },
  mxsObjType: { type: String, default: '' }, // MXS_OBJ_TYPES
  creationMode: { type: Boolean, default: false },
  confirmEdit: { type: Function, default: () => null },
})

const emit = defineEmits(['changed-params'])

const {
  lodash: { keyBy, isEqual },
} = useHelpers()
const typy = useTypy()

const store = useStore()
const isAdmin = computed(() => store.getters['user/isAdmin'])
const search_keyword = computed(() => store.state.search_keyword)

let port = ref(null)
let socket = ref(null)
let showEditBtn = ref(false)
let isEditing = ref(false)
let showConfirmDialog = ref(false)
let nodes = ref([])
let isFormValid = ref(false)
let form = ref(null)
let changedNodeMap = reactive({})

const nodeMap = computed(() => keyBy(nodes.value, 'id'))
const paramInfoMap = computed(() => keyBy(props.paramsInfo, 'name'))

const isListener = computed(() => props.mxsObjType === MXS_OBJ_TYPES.LISTENERS)
const changedNodeIds = computed(() => Object.keys(changedNodeMap))
const hasChanged = computed(() => {
  if (changedNodeIds.value.length > 0 && isFormValid.value) return true
  return false
})
const changedParams = computed(() =>
  treeToObj({
    changedNodes: Object.values(changedNodeMap),
    nodeMap: nodeMap.value,
  })
)

watch(
  nodes,
  (v) => {
    if (isServerOrListenerType(props.mxsObjType)) v.forEach((node) => setPortAndSocketValues(node))
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
  if (isAdmin)
    if (e.type === 'mouseenter') showEditBtn.value = true
    else if (e.type === 'mouseleave') showEditBtn.value = false
}

async function onChangeParam({ id, key, value }) {
  const node = nodeMap.value[id]
  if (isEqual(node.value, value)) delete changedNodeMap[id]
  else changedNodeMap[id] = { ...node, value }
  setPortAndSocketValues({ key, value })
  // Trigger form validation for handling port/socket and address params
  await typy(form.value, 'validate').safeFunction()
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
  changedNodeMap = {}
}

async function confirm() {
  await props.confirmEdit(changedParams.value)
  cleanup()
}
</script>
<template>
  <CollapsibleCtr
    :title="`${$t('parameters', 2)}`"
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
    <template v-if="!creationMode" #header-right>
      <VFadeTransition>
        <VBtn
          v-if="isEditing"
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
    <VForm ref="form" v-model="isFormValid">
      <TreeTable
        :data="data"
        :search="search_keyword"
        :expandAll="true"
        :showCellBorder="true"
        fixedLayout
        keyWidth="35%"
        valueWidth="65%"
        :keyInfoMap="paramInfoMap"
        showKeyLength
        :arrayTransform="false"
        @get-nodes="nodes = $event"
      >
        <template v-if="isEditing" #[`item.value`]="{ item }">
          <ParameterInput
            v-if="item.leaf"
            class="pa-1"
            :item="item"
            :keyInfo="$typy(paramInfoMap[item.key]).safeObjectOrEmpty"
            :creationMode="creationMode"
            :isListener="isListener"
            :portValue="port"
            :socketValue="socket"
            @on-change="onChangeParam"
          />
        </template>
      </TreeTable>
    </VForm>
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
