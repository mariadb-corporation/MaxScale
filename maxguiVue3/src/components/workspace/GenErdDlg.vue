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
import ErdTask from '@wsModels/ErdTask'
import ErdTaskTmp from '@wsModels/ErdTaskTmp'
import QueryConn from '@wsModels/QueryConn'
import Worksheet from '@wsModels/Worksheet'
import connection from '@/api/sql/connection'
import ddlEditorService from '@/services/ddlEditorService'
import worksheetService from '@/services/worksheetService'
import erdTaskService from '@/services/erdTaskService'
import queryConnService from '@/services/queryConnService'
import { queryAndParseTblDDL } from '@/store/queryHelper'
import erdHelper from '@/utils/erdHelper'
import { QUERY_CONN_BINDING_TYPES } from '@/constants/workspace'
import SelectableSchemaTableTree from '@wsComps/SelectableSchemaTableTree.vue'

const store = useStore()
const typy = useTypy()
const {
  tryAsync,
  getErrorsArr,
  dynamicColors,
  uuidv1,
  lodash: { cloneDeep },
} = useHelpers()

const selectedTargets = ref([])
const errMsg = ref('')
const name = ref('')

const gen_erd_dlg = computed(() => store.state.mxsWorkspace.gen_erd_dlg)
const charset_collation_map = computed(() => store.state.ddlEditor.charset_collation_map)
const isOpened = computed({
  get: () => gen_erd_dlg.value.is_opened,
  set: (v) => store.commit('mxsWorkspace/SET_GEN_ERD_DLG', { ...gen_erd_dlg.value, is_opened: v }),
})
const preselectedSchemas = computed(() => gen_erd_dlg.value.preselected_schemas)
const connData = computed(() => typy(gen_erd_dlg.value, 'connection').safeObject)
const connId = computed(() => typy(connData.value, 'id').safeString)
const genInNewWs = computed(() => typy(gen_erd_dlg.value, 'gen_in_new_ws').safeBoolean)
const activeRequestConfig = computed(() => Worksheet.getters('activeRequestConfig'))
const hasSavingErr = computed(() => Boolean(errMsg.value) || Boolean(!selectedTargets.value.length))
const activeWkeId = computed(() => Worksheet.getters('activeId')) // activeWkeId is also erd_task_id

watch(isOpened, (v) => {
  if (v) {
    const { id, count = 0 } = ErdTask.query().last() || {}
    name.value = `ERD ${activeWkeId.value === id ? count : count + 1}`
  } else errMsg.value = ''
})

async function handleCloneConn({ conn, config }) {
  const [e, res] = await tryAsync(connection.clone({ id: conn.id, config }))
  if (e) errMsg.value = getErrorsArr(e).join('\n')
  return typy(res, 'data.data').safeObjectOrEmpty
}

async function handleQueryData({ conn, config }) {
  await queryConnService.enableSqlQuoteShowCreate({ connId: conn.id, config })
  await ddlEditorService.querySuppData({ connId: conn.id, config })
  const [e, parsedTables] = await queryAndParseTblDDL({
    connId: conn.id,
    targets: selectedTargets.value,
    config,
    charsetCollationMap: charset_collation_map.value,
  })
  if (e) errMsg.value = getErrorsArr(e).join('\n')
  else {
    const nodeMap = parsedTables.reduce((map, parsedTable, i) => {
      const node = erdHelper.genErdNode({
        nodeData: parsedTable,
        highlightColor: dynamicColors(i),
      })
      map[node.id] = node
      return map
    }, {})

    return {
      erdTaskData: { nodeMap, is_laid_out: false },
      erdTaskTmpData: {
        graph_height_pct: 100,
        active_entity_id: '',
        key: uuidv1(),
        nodes_history: [],
        active_history_idx: 0,
      },
    }
  }
}

async function visualize() {
  const config = activeRequestConfig.value
  const connMeta = connData.value.meta
  let conn = connData.value

  if (genInNewWs.value) {
    conn = await handleCloneConn({ conn: cloneDeep(connData.value), config })
    await queryConnService.setVariables({ connId: conn.id, config })
  }
  if (conn.id) {
    const data = await handleQueryData({ conn, config })
    if (data) {
      const { erdTaskData, erdTaskTmpData } = data
      if (genInNewWs.value) visualizeInNewWs({ conn, connMeta, erdTaskData, erdTaskTmpData })
      else visualizeInCurrentWs({ erdTaskData, erdTaskTmpData })
      Worksheet.update({ where: activeWkeId.value, data: { name: name.value } })
    }
  }
}

function visualizeInNewWs({ conn, connMeta, erdTaskData, erdTaskTmpData }) {
  worksheetService.insertBlank()
  erdTaskService.initEntities({ erdTaskData, erdTaskTmpData })
  QueryConn.insert({
    data: {
      id: conn.id,
      attributes: conn.attributes,
      binding_type: QUERY_CONN_BINDING_TYPES.ERD,
      erd_task_id: activeWkeId.value,
      meta: connMeta,
    },
  })
}

function visualizeInCurrentWs({ erdTaskData, erdTaskTmpData }) {
  // Close the entity-editor-ctr before assigning new data
  ErdTaskTmp.update({
    where: activeWkeId.value,
    data: { active_entity_id: '', graph_height_pct: 100 },
  }).then(() => {
    ErdTask.update({ where: activeWkeId.value, data: erdTaskData })
    ErdTaskTmp.update({ where: activeWkeId.value, data: erdTaskTmpData })
  })
}
</script>

<template>
  <BaseDlg
    v-model="isOpened"
    :title="$t('selectObjsToVisualize')"
    saveText="visualize"
    minBodyWidth="768px"
    :allowEnterToSubmit="false"
    :hasSavingErr="hasSavingErr"
    :onSave="visualize"
  >
    <template #form-body>
      <LabelField v-model="name" :label="$t('name')" required class="mb-3" />
      <SelectableSchemaTableTree
        :connId="connId"
        :preselectedSchemas="preselectedSchemas"
        :triggerDataFetch="isOpened"
        excludeNonFkSupportedTbl
        @selected-targets="selectedTargets = $event"
      />
      <div class="err-visualizing-message-ctr mt-3">
        <p class="text-small-text mb-1" data-test="erd-support-table-info">
          {{ $t('info.erdTableSupport') }}
        </p>
        <p v-if="errMsg" class="text-error mt-2 mb-4" data-test="err-msg">
          {{ errMsg }}
        </p>
      </div>
    </template>
  </BaseDlg>
</template>

<style lang="scss" scoped>
.err-visualizing-message-ctr {
  min-height: 24px;
  .text-error {
    white-space: pre-wrap;
    line-height: 1.5rem;
  }
}
</style>
