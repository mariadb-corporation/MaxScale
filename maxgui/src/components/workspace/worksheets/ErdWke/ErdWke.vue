<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-09-09
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import ErdTask from '@wsModels/ErdTask'
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import QueryConn from '@wsModels/QueryConn'
import DiagramCtr from '@wkeComps/ErdWke/DiagramCtr.vue'
import EntityEditorCtr from '@wkeComps/ErdWke/EntityEditorCtr.vue'
import erdTaskService from '@wsServices/erdTaskService'
import workspaceService from '@wsServices/workspaceService'
import scriptGenerator from '@wkeComps/ErdWke/scriptGenerator'
import erdHelper from '@/utils/erdHelper'

const props = defineProps({
  ctrDim: { type: Object, required: true },
  wke: { type: Object, required: true },
})

const diagramCtrRef = ref(null)
const entityEditorCtrRef = ref(null)
const scriptName = ref('')
const scriptGeneratedTime = ref(null)

const store = useStore()
const typy = useTypy()
const {
  lodash: { keyBy },
  pctToPx,
  pxToPct,
  dateFormat,
  exportToJpeg,
  copyTextToClipboard,
  immutableUpdate,
} = useHelpers()

const exec_sql_dlg = computed(() => store.state.workspace.exec_sql_dlg)
const taskId = computed(() => props.wke.erd_task_id)
const erdTask = computed(() => ErdTask.find(taskId.value) || {})
const erdTaskTmp = computed(() => ErdTaskTmp.find(taskId.value) || {})
/**
 * If the users generate new ERD for existing ERD worksheet
 * or a blank ERD worksheet, erdTaskKey will be re-generated
 * so the diagram must be reinitialized
 */
const erdTaskKey = computed(() => typy(erdTaskTmp.value, 'key').safeString)
const nodeMap = computed({
  get: () => typy(erdTask.value, 'nodeMap').safeObjectOrEmpty,
  set: (v) => ErdTask.update({ where: taskId.value, data: { nodeMap: v } }),
})
const nodes = computed(() => Object.values(nodeMap.value))
const tables = computed(() => nodes.value.map((n) => n.data))
const schemas = computed(() => [...new Set(nodes.value.map((n) => n.data.options.schema))])
const refTargetMap = computed(() => keyBy(erdHelper.genRefTargets(tables.value), 'id'))
const tablesColNameMap = computed(() => erdHelper.createTablesColNameMap(tables.value))
const activeEntityId = computed(() => typy(erdTaskTmp.value, 'active_entity_id').safeString)
const graphHeightPct = computed({
  get: () => typy(erdTaskTmp.value, 'graph_height_pct').safeNumber,
  set: (v) => ErdTaskTmp.update({ where: taskId.value, data: { graph_height_pct: v } }),
})
const erGraphHeight = computed(() =>
  pctToPx({ pct: graphHeightPct.value, containerPx: props.ctrDim.height })
)
const erdDim = computed(() => ({ width: props.ctrDim.width, height: erGraphHeight.value }))
const editorDim = computed(() => ({
  width: props.ctrDim.width,
  height: props.ctrDim.height - erGraphHeight.value,
}))
const minErdPct = computed(() =>
  pxToPct({ px: activeEntityId.value ? 40 : 0, containerPx: props.ctrDim.height })
)
const maxErdPct = computed(() => 100 - minErdPct.value)
const taskName = computed(() => props.wke.name)
const conn = computed(() => QueryConn.query().where('erd_task_id', taskId.value).first() || {})
const connId = computed(() => typy(conn.value, 'id').safeString)
const activeNodeData = computed(
  () => typy(nodeMap.value, `[${activeEntityId.value}].data`).safeObjectOrEmpty
)
const actionName = computed(
  () => `Apply script ${scriptName.value} at ${scriptGeneratedTime.value}`
)

function genScript() {
  const { name, time, sql } = scriptGenerator({
    schemas: schemas.value,
    tables: tables.value,
    refTargetMap: refTargetMap.value,
    tablesColNameMap: tablesColNameMap.value,
  })
  scriptName.value = name
  scriptGeneratedTime.value = time
  return sql
}

function applyScript() {
  store.commit('workspace/SET_EXEC_SQL_DLG', {
    ...exec_sql_dlg.value,
    is_opened: true,
    editor_height: 450,
    sql: genScript(),
    on_exec: onExecuteScript,
  })
}

async function onExecuteScript() {
  await workspaceService.exeDdlScript({
    connId: connId.value,
    actionName: actionName.value,
  })
}

function exportScript() {
  const blob = new Blob([genScript()], { type: 'text/sql' })
  const url = URL.createObjectURL(blob)
  const a = document.createElement('a')
  a.href = url
  const time = dateFormat({
    value: scriptGeneratedTime.value,
    formatType: 'EEE_dd_MMM_yyyy',
  })
  a.download = `${taskName.value}_${time}.sql`
  a.click()
  URL.revokeObjectURL(url)
}

async function exportAsJpeg() {
  exportToJpeg({
    canvas: await typy(diagramCtrRef.value, 'getCanvas').safeFunction(),
    fileName: taskName.value,
  })
}

function copyScriptToClipboard() {
  copyTextToClipboard(genScript())
}

function updateDiagramNode(param) {
  typy(diagramCtrRef.value, 'updateNode').safeFunction(param)
}

function updateNodeData(data) {
  const id = activeEntityId.value
  nodeMap.value = immutableUpdate(nodeMap.value, {
    [id]: { data: { $set: data } },
  })
  erdTaskService.updateNodesHistory(nodeMap.value)
  updateDiagramNode({ id, data })
}
/**
 * Used to prevent the active entity editor from being closed or
 * having its data changed to another entity if the form is invalid.
 */
async function validateEntityEditor() {
  return activeEntityId.value
    ? await typy(entityEditorCtrRef.value, 'validate').safeFunction()
    : true
}
</script>

<template>
  <ResizablePanels
    v-model="graphHeightPct"
    :boundary="ctrDim.height"
    split="horiz"
    :minPercent="minErdPct"
    :maxPercent="maxErdPct"
    :deactivatedMaxPctZone="maxErdPct - (100 - maxErdPct) * 2"
    :disable="graphHeightPct === 100"
  >
    <template #pane-left>
      <DiagramCtr
        ref="diagramCtrRef"
        :key="erdTaskKey"
        :dim="erdDim"
        :graphHeightPct="graphHeightPct"
        :erdTask="erdTask"
        :conn="conn"
        :nodeMap="nodeMap"
        :nodes="nodes"
        :tables="tables"
        :schemas="schemas"
        :activeEntityId="activeEntityId"
        :erdTaskTmp="erdTaskTmp"
        :refTargetMap="refTargetMap"
        :tablesColNameMap="tablesColNameMap"
        :applyScript="applyScript"
        :validateEntityEditor="validateEntityEditor"
        @on-export-script="exportScript()"
        @on-export-as-jpeg="exportAsJpeg()"
        @on-copy-script-to-clipboard="copyScriptToClipboard()"
      />
    </template>
    <template #pane-right>
      <EntityEditorCtr
        v-if="activeEntityId"
        ref="entityEditorCtrRef"
        :dim="editorDim"
        :data="activeNodeData"
        :taskId="taskId"
        :connId="connId"
        :tables="tables"
        :schemas="schemas"
        :erdTaskTmp="erdTaskTmp"
        @change="updateNodeData($event)"
      />
    </template>
  </ResizablePanels>
</template>
