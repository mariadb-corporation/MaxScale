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
import Worksheet from '@wsModels/Worksheet'
import SchemaSidebar from '@wsModels/SchemaSidebar'
import queries from '@/api/sql/queries'
import { getChildNodes } from '@/store/queryHelper'
import schemaNodeHelper from '@/utils/schemaNodeHelper'
import { stringifyErrResult } from '@/utils/queryUtils'
import { NODE_TYPE_MAP, NODE_GROUP_TYPE_MAP, FK_SUPPORTED_ENGINE } from '@/constants/workspace'
import VirSchemaTree from '@wsComps/VirSchemaTree.vue'
import SchemaNodeIcon from '@wsComps/SchemaNodeIcon.vue'

const props = defineProps({
  connId: { type: String, required: true },
  preselectedSchemas: { type: Array, default: () => [] },
  triggerDataFetch: { type: Boolean, required: true },
  excludeNonFkSupportedTbl: { type: Boolean, default: false },
})

const emit = defineEmits(['selected-targets'])

const { t } = useI18n()
const { tryAsync } = useHelpers()
const typy = useTypy()

const virSchemaTreeRef = ref(null)
const selectedObjs = ref([])
const data = ref([])
const inputMsgObj = ref(null)
const queryErrMsg = ref('')
const expandedNodes = ref([])

const activeRequestConfig = computed(() => Worksheet.getters('activeRequestConfig'))
const categorizeObjs = computed(() =>
  selectedObjs.value.reduce(
    (obj, o) => {
      // SCHEMA nodes will be included in selectedObjs even though those have no tables
      if (o.type === NODE_TYPE_MAP.SCHEMA) {
        obj.emptySchemas.push(o.name)
      } else {
        obj.targets.push({
          tbl: o.name,
          schema: o.parentNameData[NODE_TYPE_MAP.SCHEMA],
        })
      }
      return obj
    },
    { targets: [], emptySchemas: [] }
  )
)
const targets = computed(() => categorizeObjs.value.targets)
const emptySchemas = computed(() => categorizeObjs.value.emptySchemas)

const preselectedSchemaNodes = computed(() =>
  data.value.filter((n) => props.preselectedSchemas.includes(n.qualified_name))
)

watch(
  () => props.triggerDataFetch,
  async (v) => {
    if (v) {
      data.value = []
      await fetchSchemas()
      handlePreselectedSchemas()
    }
  },
  { immediate: true }
)
watch(
  selectedObjs,
  (v) => {
    if (v.length) {
      if (!targets.value.length)
        inputMsgObj.value = { type: 'error', text: t('errors.emptyVisualizeSchema') }
      else if (emptySchemas.value.length)
        inputMsgObj.value = { type: 'warning', text: t('warnings.ignoredVisualizeSchemas') }
      else inputMsgObj.value = null
    } else inputMsgObj.value = null
  },
  { deep: true }
)
watch(
  targets,
  (v) => {
    emit('selected-targets', v)
  },
  { deep: true, immediate: true }
)

async function fetchSchemas() {
  const [e, res] = await tryAsync(
    queries.post({
      id: props.connId,
      body: { sql: SchemaSidebar.getters('schemaSql') },
      config: activeRequestConfig.value,
    })
  )

  if (e) queryErrMsg.value = t('errors.retrieveSchemaObj')
  else {
    const result = typy(res, 'data.data.attributes.results[0]').safeObject
    if (typy(result, 'errno').isDefined) queryErrMsg.value = stringifyErrResult(result)
    else {
      queryErrMsg.value = ''
      data.value = schemaNodeHelper.genNodes({
        queryResult: result,
        nodeAttrs: { isEmptyChildren: true },
      })
    }
  }
}

async function handlePreselectedSchemas() {
  const selectedItems = []
  for (const node of preselectedSchemaNodes.value) {
    // expand the node to fetch its children automatically
    await virSchemaTreeRef.value.toggleNode(node)
    if (typy(node, 'children').safeArray.length)
      selectedItems.push(...typy(node, 'children').safeArray)
    else selectedItems.push(node)
  }
  selectedObjs.value = selectedItems
}

async function loadTables(node) {
  const children = await getChildNodes({
    connId: props.connId,
    nodeGroup: schemaNodeHelper.genNodeGroup({ parentNode: node, type: NODE_GROUP_TYPE_MAP.TBL_G }),
    nodeAttrs: { isLeaf: true },
    config: activeRequestConfig.value,
  })
  if (props.excludeNonFkSupportedTbl)
    return children.filter((n) => n.data.ENGINE === FK_SUPPORTED_ENGINE)
  return children
}

function onTreeChanges(tree) {
  data.value = tree
}
</script>

<template>
  <div class="selectable-schema-table-tree">
    <VirSchemaTree
      ref="virSchemaTreeRef"
      v-model:expandedNodes="expandedNodes"
      v-model:selectedNodes="selectedObjs"
      class="vir-schema-tree border--separator pa-2 rounded"
      :data="data"
      height="100%"
      :loadChildren="loadTables"
      @on-tree-changes="onTreeChanges"
    >
      <template #label="{ node }">
        <SchemaNodeIcon :node="node" :size="16" />
        <span class="ml-1 text-truncate d-inline-block node-name">{{ node.name }} </span>
      </template>
    </VirSchemaTree>
    <div class="input-message-ctr mt-3">
      <p
        v-if="inputMsgObj"
        :class="`text-body-2 mb-4 text-${inputMsgObj.type}`"
        data-test="input-msg"
      >
        {{ inputMsgObj.text }}
      </p>
      <p v-if="queryErrMsg" class="text-body-2 text-error mt-2 mb-4" data-test="query-err-msg">
        {{ queryErrMsg }}
      </p>
    </div>
  </div>
</template>

<style lang="scss" scoped>
.input-message-ctr {
  min-height: 24px;
}
.vir-schema-tree {
  max-height: 500px;
  background-color: colors.$light-gray;
}
</style>
