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
import { MXS_OBJ_TYPES, MONITOR_OP_TYPES, MRDB_MON } from '@/constants'
import PageHeader from '@/components/details/PageHeader.vue'
import DurationInput from '@/components/details/DurationInput.vue'

const props = defineProps({
  item: { type: Object, required: true },
  onCountDone: { type: Function },
  successCb: { type: Function, required: true },
  csStatus: { type: Object, default: () => ({}) },
  fetchCsStatus: { type: Function, required: true },
})
const emit = defineEmits(['is-calling-op'])
const store = useStore()
const typy = useTypy()
const { t } = useI18n()

const {
  STOP,
  START,
  DESTROY,
  RESET_REP,
  RELEASE_LOCKS,
  FAILOVER,
  CS_STOP_CLUSTER,
  CS_START_CLUSTER,
  CS_SET_READWRITE,
  CS_SET_READONLY,
  CS_ADD_NODE,
  CS_REMOVE_NODE,
} = MONITOR_OP_TYPES

const state = computed(() => typy(props.item, 'attributes.state').safeString)
const fetchObjData = useFetchObjData()
const { map: allOps, handler: opHandler } = useMonitorOpMap(state)
const module = computed(() => typy(props.item, 'attributes.module').safeString)
const isColumnStoreCluster = computed(() =>
  Boolean(typy(props.item, 'attributes.parameters.cs_admin_api_key').safeString)
)
const currCsNodesData = computed(() => {
  let nodes = {}
  Object.keys(props.csStatus).forEach((key) => {
    const v = props.csStatus[key]
    if (typy(v).isObject) nodes[key] = v
  })
  return nodes
})
const currCsNodeIds = computed(() => Object.keys(currCsNodesData.value))
const isClusterStopped = computed(() =>
  Object.values(currCsNodesData.value).every((v) => v.services.length === 0)
)
const isClusterReadonly = computed(() =>
  Object.values(currCsNodesData.value).every((v) => v.cluster_mode === 'readonly')
)
const operationMatrix = computed(() => {
  const { attributes: { monitor_diagnostics: { primary = false } = {}, parameters = {} } = {} } =
    props.item
  let ops = [allOps.value[STOP], allOps.value[START], allOps.value[DESTROY]]
  if (module.value === MRDB_MON) {
    ops = [...ops, { divider: true }, allOps.value[RESET_REP]]
    // only add the release_locks option when this cluster is a primary one
    if (primary) ops.push(allOps.value[RELEASE_LOCKS])
    // only add the failover option when auto_failover is false
    if (!typy(parameters, 'auto_failover').safeBoolean) ops.push(allOps.value[FAILOVER])
    // Add ColumnStore operations
    if (isColumnStoreCluster.value) {
      ops = [
        ...ops,
        { divider: true },
        { subheader: t('csOps') },
        { ...allOps.value[CS_STOP_CLUSTER], disabled: isClusterStopped.value },
        { ...allOps.value[CS_START_CLUSTER], disabled: !isClusterStopped.value },
        { ...allOps.value[CS_SET_READONLY], disabled: isClusterReadonly.value },
        { ...allOps.value[CS_SET_READWRITE], disabled: !isClusterReadonly.value },
        allOps.value[CS_ADD_NODE],
        allOps.value[CS_REMOVE_NODE],
      ]
    }
  }
  return [ops]
})

let allServerIds = ref([])
let timeout = ref('')
let targetClusterNode = ref(null)

async function onConfirmDlgOpened(confirmDlg) {
  if (confirmDlg.type === MONITOR_OP_TYPES.CS_ADD_NODE) {
    const allServers = await fetchObjData({ type: MXS_OBJ_TYPES.SERVERS, fields: ['id'] })
    allServerIds.value = allServers.map((item) => item.id)
  }
  timeout.value = '1m'
}

/**
 *
 * @param {String} param.type - operation type. Check MONITOR_OP_TYPES
 * @param {Object} param.meta - meta data.
 */
function validateRes({ type, meta }) {
  switch (type) {
    case CS_ADD_NODE:
    case CS_REMOVE_NODE:
      return (
        (type === CS_REMOVE_NODE && !currCsNodesData.value[targetClusterNode.value]) ||
        (type === CS_ADD_NODE && currCsNodesData.value[targetClusterNode.value])
      )
    case CS_STOP_CLUSTER:
    case CS_START_CLUSTER:
      return (
        (type === CS_STOP_CLUSTER && isClusterStopped.value) ||
        (type === CS_START_CLUSTER && !isClusterStopped.value)
      )
    case CS_SET_READWRITE:
    case CS_SET_READONLY: {
      return (
        (type === CS_SET_READONLY && meta['cluster-mode'] === 'readonly') ||
        (type === CS_SET_READWRITE && meta['cluster-mode'] === 'readwrite')
      )
    }
  }
}

/**
 * @param {String} opType - operation type. Check MONITOR_OP_TYPES
 */
function csOpParamsCreator(opType) {
  const {
    CS_STOP_CLUSTER,
    CS_START_CLUSTER,
    CS_ADD_NODE,
    CS_REMOVE_NODE,
    CS_SET_READWRITE,
    CS_SET_READONLY,
  } = MONITOR_OP_TYPES
  switch (opType) {
    case CS_ADD_NODE:
    case CS_REMOVE_NODE:
      return `&${targetClusterNode.value}&${timeout.value}`
    case CS_STOP_CLUSTER:
    case CS_START_CLUSTER:
    case CS_SET_READWRITE:
    case CS_SET_READONLY:
      return `&${timeout.value}`
  }
}

async function onConfirmOp({ op, id }) {
  let param = {
    op,
    id,
    module: module.value,
    successCb: props.successCb,
  }
  if (isColumnStoreCluster.value)
    param.csPayload = {
      pollingResInterval: 1000,
      asyncCmdSuccessHandler: async (meta) => {
        const action = t(`monitorOps.actions.${op.type}`)
        await props.fetchCsStatus({
          id,
          module: param.module,
          state: state.value,
          pollingInterval: 1000,
        })
        let msgs = [],
          msgType = 'success'
        if (validateRes({ type: op.type, meta })) msgs = [`${action} successfully`]
        else {
          msgs = [`Failed to ${action}`]
          msgType = 'error'
        }
        await param.successCb()
        store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', { text: msgs, type: msgType })
      },
      opParams: { module: param.module, params: csOpParamsCreator(op.type) },
    }
  emit('is-calling-op', true)
  await opHandler(param)
  emit('is-calling-op', false)
}
</script>

<template>
  <PageHeader
    :item="item"
    :type="MXS_OBJ_TYPES.MONITORS"
    showStateIcon
    :stateLabel="state"
    :operationMatrix="operationMatrix"
    :onConfirm="onConfirmOp"
    :onCountDone="onCountDone"
    :defFormType="MXS_OBJ_TYPES.SERVERS"
    :onConfirmDlgOpened="onConfirmDlgOpened"
    showOperationAsList
  >
    <template #state-append>
      <span class="text-grayed-out text-body-2">
        |
        <span class="resource-module">{{ module }}</span>
      </span>
    </template>
    <template #confirm-dlg-body-append="{ confirmDlg }">
      <template
        v-if="
          confirmDlg.type === MONITOR_OP_TYPES.CS_REMOVE_NODE ||
          confirmDlg.type === MONITOR_OP_TYPES.CS_ADD_NODE
        "
      >
        <label class="field__label text-small-text label-required" for="target-cluster-node-input">
          {{ $t('hostname/IP') }}
        </label>
        <VCombobox
          v-model="targetClusterNode"
          :items="
            confirmDlg.type === MONITOR_OP_TYPES.CS_REMOVE_NODE
              ? currCsNodeIds
              : allServerIds.filter((id) => !currCsNodeIds.includes(id))
          "
          class="mb-3"
          :placeholder="$t('selectNodeOrEnterIp')"
          :rules="[(v) => !!v || $t('errors.requiredInput', { inputName: $t('hostname/IP') })]"
          hide-details="auto"
          id="target-cluster-node-input"
        />
      </template>
      <template v-if="isColumnStoreCluster">
        <DurationInput v-model="timeout" />
      </template>
    </template>
    <template v-for="(_, name) in $slots" #[name]="slotData">
      <slot :name="name" v-bind="slotData" />
    </template>
  </PageHeader>
</template>
