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
import TblEditor from '@wsModels/TblEditor'
import Worksheet from '@wsModels/Worksheet'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import TblStructureEditor from '@wsComps/TblStructureEditor/TblStructureEditor.vue'
import workspaceService from '@wsServices/workspaceService'
import queryConnService from '@wsServices/queryConnService'
import schemaInfoService from '@wsServices/schemaInfoService'
import { queryTblIdentifiers } from '@/store/queryHelper'
import { UNPARSED_TBL_PLACEHOLDER } from '@/constants/workspace'

const props = defineProps({
  dim: { type: Object, required: true },
  queryEditorTmp: { type: Object, required: true },
  queryTab: { type: Object, required: true },
})

const typy = useTypy()
const {
  lodash: { cloneDeep, debounce },
  quotingIdentifier,
} = useHelpers()

const hintedRefTargets = ref([])

const queryTabTmp = computed(() => QueryTabTmp.find(props.queryTab.id) || {})
const tblEditor = computed(() => TblEditor.find(props.queryTab.id) || {})
const queryTabConn = computed(() => queryConnService.findQueryTabConn(props.queryTab.id))
const isFetchingData = computed(() => typy(tblEditor.value, 'is_fetching').safeBoolean)
const isCreating = computed(() => typy(tblEditor.value, 'active_node').isNull)
const connId = computed(() => typy(queryTabConn.value, 'id').safeString)
const activeRequestConfig = computed(() => Worksheet.getters('activeRequestConfig'))
const activeSpec = computed({
  get: () => typy(tblEditor.value, 'active_spec').safeString,
  set: (v) => TblEditor.update({ where: props.queryTab.id, data: { active_spec: v } }),
})
const persistentData = computed({
  get: () => typy(tblEditor.value, 'data').safeObjectOrEmpty,
  set: (v) => TblEditor.update({ where: props.queryTab.id, data: { data: v } }),
})
/**
 * staging data for the "alter" mode. i.e. Changes to the table won't be persisted
 * until the script is successfully applied
 */
const stagingData = computed({
  get: () => typy(queryTabTmp.value, 'alter_editor_staging_data').safeObjectOrEmpty,
  set: (v) =>
    QueryTabTmp.update({ where: props.queryTab.id, data: { alter_editor_staging_data: v } }),
})
const data = computed({
  get: () => (isCreating.value ? persistentData.value : stagingData.value),
  set: (v) => {
    if (isCreating.value) persistentData.value = v
    else stagingData.value = v
  },
})
const nonSystemSchemas = computed(() =>
  typy(props.queryEditorTmp, 'db_tree').safeArray.reduce((acc, n) => {
    if (!n.isSys) acc.push(n.name)
    return acc
  }, [])
)
const schema = computed(() => typy(data.value, 'options.schema').safeString)
const lookupTables = computed(() => ({ [data.value.id]: data.value }))
const skipSchemaCreation = computed(() => nonSystemSchemas.value.includes(schema.value))

watch(
  isFetchingData,
  (v) => {
    // Clone persistent data to staging data for altering table mode
    if (!isCreating.value && !v && typy(stagingData.value).isEmptyObject)
      QueryTabTmp.update({
        where: props.queryTab.id,
        data: { alter_editor_staging_data: cloneDeep(persistentData.value) },
      })
  },
  { immediate: true }
)

watch(
  schema,
  debounce(async (v) => {
    if (v) {
      const tblNames = await queryTblIdentifiers({
        connId: connId.value,
        config: activeRequestConfig.value,
        schemaName: v,
      })
      hintedRefTargets.value = tblNames.map((name) => {
        const qualifiedName = `${quotingIdentifier(v)}.${quotingIdentifier(name)}`
        return {
          id: `${UNPARSED_TBL_PLACEHOLDER}${qualifiedName}`,
          text: qualifiedName,
          name,
          schema: v,
        }
      })
    }
  }, 1000),
  { immediate: true }
)

onMounted(
  async () =>
    await schemaInfoService.querySuppData({
      connId: connId.value,
      config: activeRequestConfig.value,
    })
)
/**
 * Execute script for altering or creating a table.
 */
async function onExecute() {
  await workspaceService.exeDdlScript({
    connId: connId.value,
    schema: persistentData.value.options.schema,
    name: persistentData.value.options.name,
    successCb: () => {
      // alter successfully altering the table, store the stagingData to persistentData
      if (!isCreating.value) persistentData.value = cloneDeep(stagingData.value)
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
      v-if="!$typy(data).isEmptyObject"
      :key="queryTab.id"
      v-model="data"
      v-model:activeSpec="activeSpec"
      :dim="dim"
      :initialData="isCreating ? {} : persistentData"
      :isCreating="isCreating"
      :skipSchemaCreation="skipSchemaCreation"
      :connData="{ id: connId, config: activeRequestConfig }"
      :onExecute="onExecute"
      :lookupTables="lookupTables"
      :hintedRefTargets="hintedRefTargets"
      :schemas="nonSystemSchemas"
    />
  </VCard>
</template>
