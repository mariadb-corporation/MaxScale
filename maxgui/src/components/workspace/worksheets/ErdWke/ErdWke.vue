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
import ErdTask from '@wsModels/ErdTask'
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import QueryConn from '@wsModels/QueryConn'
import DiagramCtr from '@wkeComps/ErdWke/DiagramCtr.vue'
import EntityEditorCtr from '@wkeComps/ErdWke/EntityEditorCtr.vue'
import workspaceService from '@wsServices/workspaceService'
import TableScriptBuilder from '@/utils/TableScriptBuilder.js'
import SqlCommenter from '@/utils/SqlCommenter.js'
import erdHelper from '@/utils/erdHelper'
import { formatSQL } from '@/utils/queryUtils'

const props = defineProps({
  ctrDim: { type: Object, required: true },
  wke: { type: Object, required: true },
})

const diagramCtrRef = ref(null)
const scriptName = ref('')
const scriptGeneratedTime = ref(null)
const isFormValid = ref(true)

const sqlCommenter = new SqlCommenter()
const store = useStore()
const typy = useTypy()
const {
  lodash: { keyBy },
  pctToPx,
  pxToPct,
  quotingIdentifier,
  dateFormat,
  exportToJpeg,
  copyTextToClipboard,
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
const nodeMap = computed(() => typy(erdTask.value, 'nodeMap').safeObjectOrEmpty)
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

function genScript() {
  let parts = [],
    tablesFks = []
  // new schemas
  schemas.value.forEach((s, i) => {
    if (i === 0) parts.push(sqlCommenter.genSection('Create schemas'))
    const schema = quotingIdentifier(s)
    parts.push(`CREATE SCHEMA IF NOT EXISTS ${schema};`)
  })
  // new tables
  tables.value.forEach((tbl, i) => {
    if (i === 0) parts.push(sqlCommenter.genSection('Create tables'))
    const builder = new TableScriptBuilder({
      initialData: {},
      stagingData: tbl,
      refTargetMap: refTargetMap.value,
      tablesColNameMap: tablesColNameMap.value,
      options: {
        isCreating: true,
        skipSchemaCreation: true,
        skipFkCreation: true,
      },
    })
    parts.push(builder.build())
    const fks = builder.buildNewFkSQL()
    if (fks) tablesFks.push(fks)
  })

  if (tablesFks.length) {
    parts.push(sqlCommenter.genSection('Add new tables constraints'))
    parts.push(tablesFks.join(''))
  }

  const { name, time, content } = sqlCommenter.genHeader()
  scriptName.value = name
  scriptGeneratedTime.value = time
  let sql = formatSQL(parts.join('\n'))
  sql = `${content}\n\n${sql}`
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
    actionName: `Apply script ${scriptName.value} at ${scriptGeneratedTime.value}`,
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
  exportToJpeg({ canvas: await diagramCtrRef.value.getCanvas(), fileName: taskName.value })
}

function copyScriptToClipboard() {
  copyTextToClipboard(genScript())
}
function updateDiagramNode(param) {
  diagramCtrRef.value.updateNode(param)
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
        :isFormValid="isFormValid"
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
        @on-export-script="exportScript"
        @on-export-as-jpeg="exportAsJpeg"
        @on-copy-script-to-clipboard="copyScriptToClipboard"
      />
    </template>
    <template #pane-right>
      <EntityEditorCtr
        v-show="activeEntityId"
        :dim="editorDim"
        :taskId="taskId"
        :connId="connId"
        :nodeMap="nodeMap"
        :tables="tables"
        :schemas="schemas"
        :activeEntityId="activeEntityId"
        :erdTaskTmp="erdTaskTmp"
        :updateNode="updateDiagramNode"
        @is-form-valid="isFormValid = $event"
      />
    </template>
  </ResizablePanels>
</template>
