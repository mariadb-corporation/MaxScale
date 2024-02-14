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
import { isServerOrListenerType } from '@/utils/dataTableHelpers'
import ParameterInput from '@/components/common/ParametersTable/ParameterInput.vue'
import { MXS_OBJ_TYPES } from '@/constants'

const props = defineProps({
  data: { type: Object, required: true },
  paramsInfo: { type: Array, required: true },
  mxsObjType: { type: String, default: '' }, // MXS_OBJ_TYPES
  creationMode: { type: Boolean, default: false },
})

const {
  lodash: { keyBy },
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
let changedParamMap = reactive({})

const paramInfoMap = computed(() => keyBy(props.paramsInfo, 'name'))

const isListener = computed(() => props.mxsObjType === MXS_OBJ_TYPES.LISTENERS)

watch(
  nodes,
  (v) => {
    if (isServerOrListenerType(props.mxsObjType)) v.forEach((node) => setPortAndSocketValues(node))
  },
  { immediate: true }
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

async function onChangeParam(param) {
  changedParamMap[param.key] = param.value
  setPortAndSocketValues(param)
  // Trigger form validation for handling port/socket and address param
  await typy(form.value, 'validate').safeFunction()
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
    <template v-if="!creationMode" v-slot:header-right>
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
  </CollapsibleCtr>
</template>
