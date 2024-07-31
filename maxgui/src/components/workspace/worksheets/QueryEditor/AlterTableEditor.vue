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
import AlterEditor from '@wsModels/AlterEditor'
import Worksheet from '@wsModels/Worksheet'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import TblStructureEditor from '@wsComps/TblStructureEditor/TblStructureEditor.vue'
import workspaceService from '@wsServices/workspaceService'
import queryConnService from '@wsServices/queryConnService'
import { NODE_TYPE_MAP, NODE_GROUP_TYPE_MAP, UNPARSED_TBL_PLACEHOLDER } from '@/constants/workspace'

const props = defineProps({
  dim: { type: Object, required: true },
  queryEditorTmp: { type: Object, required: true },
  queryTab: { type: Object, required: true },
})

const typy = useTypy()
const {
  lodash: { cloneDeep },
} = useHelpers()

const queryTabTmp = computed(() => QueryTabTmp.find(props.queryTab.id) || {})
const alterEditor = computed(() => AlterEditor.find(props.queryTab.id) || {})
const queryTabConn = computed(() => queryConnService.findQueryTabConn(props.queryTab.id))
const isFetchingData = computed(() => typy(alterEditor.value, 'is_fetching').safeBoolean)
const initialData = computed(() => typy(alterEditor.value, 'data').safeObjectOrEmpty)
const connId = computed(() => typy(queryTabConn.value, 'id').safeString)
const activeRequestConfig = computed(() => Worksheet.getters('activeRequestConfig'))
const alterEditorStagingData = computed(
  () => typy(queryTabTmp.value, 'alter_editor_staging_data').safeObjectOrEmpty
)
const stagingData = computed({
  get: () => alterEditorStagingData.value,
  set: (data) => {
    QueryTabTmp.update({
      where: props.queryTab.id,
      data: { alter_editor_staging_data: data },
    })
  },
})
const activeSpec = computed({
  get: () => typy(alterEditor.value, 'active_spec').safeString,
  set: (v) => {
    AlterEditor.update({
      where: props.queryTab.id,
      data: { active_spec: v },
    })
  },
})
const schema = computed(() => typy(stagingData.value, 'options.schema').safeString)
const tblName = computed(() => typy(stagingData.value, 'options.name').safeString)
const sidebarSchemaNode = computed(() =>
  typy(props.queryEditorTmp, 'db_tree').safeArray.find((n) => n.name === schema.value)
)
const tablesInSchema = computed(() => {
  const schemaGroupNode = typy(sidebarSchemaNode.value, 'children').safeArray.find(
    (n) => n.type === NODE_GROUP_TYPE_MAP.TBL_G
  )
  return typy(schemaGroupNode, 'children').safeArray.filter((n) => n.name !== tblName.value)
})
const hintedRefTargets = computed(() =>
  tablesInSchema.value.map((n) => ({
    id: `${UNPARSED_TBL_PLACEHOLDER}${n.qualified_name}`,
    text: n.qualified_name,
    name: n.name,
    schema: n.parentNameData[NODE_TYPE_MAP.SCHEMA],
  }))
)

watch(
  isFetchingData,
  (v) => {
    if (!v && typy(alterEditorStagingData.value).isEmptyObject) {
      QueryTabTmp.update({
        where: props.queryTab.id,
        data: {
          alter_editor_staging_data: cloneDeep(initialData.value),
        },
      })
    }
  },
  { immediate: true }
)

async function onExecute() {
  await workspaceService.exeDdlScript({
    connId: connId.value,
    schema: initialData.value.options.schema,
    name: initialData.value.options.name,
    successCb: () => {
      const data = cloneDeep(stagingData.value)
      AlterEditor.update({ where: props.queryTab.id, data: { data } })
    },
  })
}
</script>

<template>
  <VCard
    class="alter-table-editor no-shadow fill-height"
    :loading="isFetchingData ? 'primary' : false"
  >
    <TblStructureEditor
      v-if="!$typy(stagingData).isEmptyObject"
      :key="queryTab.id"
      v-model="stagingData"
      v-model:activeSpec="activeSpec"
      :dim="dim"
      :initialData="initialData"
      :connData="{ id: connId, config: activeRequestConfig }"
      :onExecute="onExecute"
      :lookupTables="{ [stagingData.id]: stagingData }"
      :hintedRefTargets="hintedRefTargets"
    />
  </VCard>
</template>
