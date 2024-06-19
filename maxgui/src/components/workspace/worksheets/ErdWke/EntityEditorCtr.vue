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
import ErdTask from '@wsModels/ErdTask'
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import Worksheet from '@wsModels/Worksheet'
import DdlEditor from '@wsComps/DdlEditor/DdlEditor.vue'
import ddlEditorService from '@wsServices/ddlEditorService'
import erdTaskService from '@wsServices/erdTaskService'

const props = defineProps({
  dim: { type: Object, required: true },
  taskId: { type: String, required: true },
  connId: { type: String, required: true },
  nodeMap: { type: Object, required: true },
  tables: { type: Array, required: true },
  schemas: { type: Array, required: true },
  activeEntityId: { type: String, required: true },
  erdTaskTmp: { type: Object, required: true },
  updateNode: { type: Function, required: true },
})

const stagingData = ref(null)

const {
  lodash: { keyBy, isEqual, cloneDeep },
  immutableUpdate,
} = useHelpers()
const typy = useTypy()
const store = useStore()
const { t } = useI18n()

const ddlEditorRef = ref(null)

const lookupTables = computed(() => keyBy(props.tables, 'id'))
const stagingActiveNode = computed(() => props.nodeMap[props.activeEntityId])
const stagingInitialData = computed(() => typy(stagingActiveNode.value, 'data').safeObjectOrEmpty)
const activeRequestConfig = computed(() => Worksheet.getters('activeRequestConfig'))
const activeSpec = computed({
  get: () => typy(props.erdTaskTmp, 'active_spec').safeString,
  set: (v) => {
    ErdTaskTmp.update({ where: props.taskId, data: { active_spec: v } })
  },
})

watch(
  stagingInitialData,
  (v) => {
    if (!typy(v).isEmptyObject && !isEqual(v, stagingData.value)) {
      typy(unwatch_stagingData).safeFunction()
      stagingData.value = cloneDeep(v)
      watch_stagingData()
    }
  },
  { deep: true, immediate: true }
)
watch(
  () => props.connId,
  async (v) => {
    if (v)
      await ddlEditorService.querySuppData({
        connId: props.connId,
        config: activeRequestConfig.value,
      })
  },
  { immediate: true }
)

let unwatch_stagingData

onBeforeUnmount(() => typy(unwatch_stagingData).safeFunction())

function watch_stagingData() {
  unwatch_stagingData = watch(
    stagingData,
    (data) => {
      const id = props.activeEntityId
      const nodeMap = immutableUpdate(props.nodeMap, { [id]: { data: { $set: data } } })
      ErdTask.update({ where: props.taskId, data: { nodeMap } })
      erdTaskService.updateNodesHistory(nodeMap)
      // update the node in the diagram
      props.updateNode({ id, data })
    },
    { deep: true }
  )
}

async function close() {
  if (await ddlEditorRef.value.validate())
    ErdTaskTmp.update({
      where: props.taskId,
      data: { graph_height_pct: 100, active_entity_id: '' },
    })
  else
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
      text: [t('errors.requiredInputs')],
      type: 'error',
    })
}
</script>

<template>
  <DdlEditor
    v-if="stagingData"
    ref="ddlEditorRef"
    v-model="stagingData"
    v-model:activeSpec="activeSpec"
    class="fill-height border-top--table-border er-editor-ctr"
    :dim="dim"
    :initialData="{}"
    isCreating
    :schemas="schemas"
    :lookupTables="lookupTables"
    :connData="{ id: connId, config: activeRequestConfig }"
    :showApplyBtn="false"
  >
    <template #toolbar-append>
      <VSpacer />
      <TooltipBtn square size="small" variant="text" color="error" @click="close">
        <template #btn-content>
          <VIcon size="12" icon="mxs:close" />
        </template>
        {{ $t('close') }}
      </TooltipBtn>
    </template>
  </DdlEditor>
</template>
