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
import Worksheet from '@wsModels/Worksheet'
import ResultSetTable from '@wkeComps/QueryEditor/ResultSetTable.vue'
import queries from '@/api/sql/queries'
import schemaNodeHelper from '@/utils/schemaNodeHelper'
import { NODE_TYPES, INSIGHT_SPECS } from '@/constants/workspace'
import { formatSQL } from '@/utils/queryUtils'
import { watch } from 'vue'

const props = defineProps({
  dim: { type: Object, required: true },
  conn: { type: Object, required: true },
  node: { type: Object, required: true },
  activeSpec: { type: String, required: true },
  specs: { type: Object, required: true },
  nodeType: { type: String, required: true },
})

const store = useStore()
const typy = useTypy()
const { escapeSingleQuote, quotingIdentifier, getErrorsArr, tryAsync } = useHelpers()

const analyzedData = ref({})
const isFetching = ref(true)

const specData = computed(() => typy(analyzedData.value, `[${props.activeSpec}]`).safeObject)
const isSchemaNode = computed(() => props.nodeType === NODE_TYPES.SCHEMA)
const schemaName = computed(() => schemaNodeHelper.getSchemaName(props.node))
const specQueryMap = computed(() => {
  const { qualified_name } = props.node

  const nodeIdentifier = quotingIdentifier(props.node.name)
  const nodeLiteralStr = `'${escapeSingleQuote(props.node.name)}'`

  const schemaIdentifier = quotingIdentifier(schemaName.value)
  const schemaLiteralStr = `'${escapeSingleQuote(schemaName.value)}'`

  const { CREATION_INFO, DDL, TABLES, VIEWS, COLUMNS, INDEXES, TRIGGERS, SP, FN } = INSIGHT_SPECS
  return Object.values(props.specs).reduce((map, spec) => {
    switch (spec) {
      case CREATION_INFO:
      case DDL:
        if (props.nodeType === NODE_TYPES.TRIGGER)
          map[spec] = `SHOW CREATE ${props.nodeType} ${nodeIdentifier}`
        else map[spec] = `SHOW CREATE ${props.nodeType} ${qualified_name}`
        break
      case TABLES:
      case VIEWS:
        map[spec] = `SHOW TABLE STATUS FROM ${qualified_name} WHERE Comment ${
          spec === TABLES ? '<>' : '='
        } 'VIEW'`
        break
      case COLUMNS:
      case INDEXES: {
        let tbl = spec === COLUMNS ? 'INFORMATION_SCHEMA.COLUMNS' : 'INFORMATION_SCHEMA.STATISTICS'
        let query = `SELECT * FROM ${tbl} WHERE TABLE_SCHEMA = ${schemaLiteralStr}`
        if (!isSchemaNode.value) query += ` AND TABLE_NAME = ${nodeLiteralStr}`
        map[spec] = query
        break
      }
      case TRIGGERS: {
        let query = `SHOW TRIGGERS FROM ${schemaIdentifier}`
        if (!isSchemaNode.value) query += ` WHERE \`Table\` = ${nodeLiteralStr}`
        map[spec] = query
        break
      }
      case SP:
      case FN:
        map[spec] = `SHOW ${
          spec === SP ? 'PROCEDURE' : 'FUNCTION'
        } STATUS WHERE Db = ${schemaLiteralStr}`
        break
    }
    return map
  }, {})
})
const isFilteredSpec = computed(() =>
  Object.keys(excludedColumnsBySpec.value).includes(props.activeSpec)
)
const ddlData = computed(() => {
  let ddl = ''
  switch (props.nodeType) {
    case NODE_TYPES.TRIGGER:
    case NODE_TYPES.SP:
    case NODE_TYPES.FN:
      ddl = typy(analyzedData.value, `[${props.activeSpec}]data[0][2]`).safeString
      break
    case NODE_TYPES.VIEW:
    default:
      ddl = typy(analyzedData.value, `[${props.activeSpec}]data[0][1]`).safeString
  }
  return formatSQL(ddl)
})

const excludedColumnsBySpec = computed(() => {
  const { COLUMNS, INDEXES, TRIGGERS, SP, FN } = INSIGHT_SPECS
  const specs = [COLUMNS, INDEXES, TRIGGERS, SP, FN]
  let cols = ['TABLE_CATALOG', 'TABLE_SCHEMA']
  if (!isSchemaNode.value) cols.push('TABLE_NAME')
  return specs.reduce((map, spec) => {
    switch (spec) {
      case INDEXES:
        map[spec] = [...cols, 'INDEX_SCHEMA']
        break
      case TRIGGERS:
        map[spec] = isSchemaNode.value ? [] : ['Table']
        break
      case SP:
      case FN:
        map[spec] = ['Db', 'Type']
        break
      default:
        map[spec] = cols
    }
    return map
  }, {})
})

const headers = computed(() =>
  typy(specData.value, 'fields').safeArray.map((field) => ({
    text: field,
    hidden: isFilteredSpec.value && excludedColumnsBySpec.value[props.activeSpec].includes(field),
  }))
)

watch(
  () => props.activeSpec,
  async (v) => {
    if (!analyzedData.value[v]) await fetch(v)
  },
  { immediate: true }
)

async function fetch(spec) {
  const sql = specQueryMap.value[spec]
  if (sql) {
    isFetching.value = true
    const result = await query(sql)
    analyzedData.value[spec] = result
  }
  isFetching.value = false
}

async function query(sql) {
  const [e, res] = await tryAsync(
    queries.post({
      id: props.conn.id,
      body: { sql },
      config: Worksheet.getters('activeRequestConfig'),
    })
  )
  if (e) store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', { text: getErrorsArr(e), type: 'error' })
  return typy(res, 'data.data.attributes.results[0]').safeObjectOrEmpty
}

async function fetchAll() {
  for (const spec of Object.values(props.specs)) await fetch(spec)
}
</script>

<template>
  <VProgressLinear v-if="isFetching" indeterminate color="primary" />
  <KeepAlive v-else>
    <SqlEditor
      v-if="activeSpec === specs.DDL"
      :modelValue="ddlData"
      readOnly
      class="fill-height"
      :options="{ contextmenu: false, fontSize: 14 }"
      skipRegCompleters
      whiteBg
    />
    <ResultSetTable
      v-else-if="$typy(specData, 'fields').isDefined"
      :key="activeSpec"
      :data="specData"
      :customHeaders="headers"
      :height="dim.height"
      :width="dim.width"
      :hasInsertOpt="false"
      showGroupBy
    >
      <template #right-table-tools-prepend>
        <TooltipBtn
          class="mr-2"
          size="small"
          :width="36"
          :min-width="'unset'"
          density="comfortable"
          color="primary"
          variant="outlined"
          :disabled="isFetching"
          @click="fetchAll"
        >
          <template #btn-content>
            <VIcon size="14" icon="mxs:reload" />
          </template>
          {{ $t('reload') }}
        </TooltipBtn>
      </template>
    </ResultSetTable>
  </KeepAlive>
</template>
