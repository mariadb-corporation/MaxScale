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
import QueryTabTmp from '@wsModels/QueryTabTmp'
import InsightViewer from '@wsModels/InsightViewer'
import InsightViewerTabItem from '@wkeComps/QueryEditor/InsightViewerTabItem.vue'
import queryConnService from '@wsServices/queryConnService'
import queryResultService from '@wsServices/queryResultService'
import schemaNodeHelper from '@/utils/schemaNodeHelper'
import { getStatementClasses, enforceLimitOffset } from '@/utils/sqlLimiter'
import { NODE_TYPE_MAP, INSIGHT_SPEC_MAP } from '@/constants/workspace'

const props = defineProps({
  dim: { type: Object, required: true },
  queryTab: { type: Object, required: true },
})

const tabNavHeight = 24
const typy = useTypy()
const {
  lodash: { pickBy, pick },
  escapeSingleQuote,
  quotingIdentifier,
} = useHelpers()
const store = useStore()

const query_row_limit = computed(() => store.state.prefAndStorage.query_row_limit)
const insightViewer = computed(() => InsightViewer.find(props.queryTab.id) || {})
const activeSpec = computed({
  get: () => typy(insightViewer.value, 'active_spec').safeString,
  set: (v) => InsightViewer.update({ where: props.queryTab.id, data: { active_spec: v } }),
})
const queryTabConn = computed(() => queryConnService.findQueryTabConn(props.queryTab.id))
const node = computed(() => typy(insightViewer.value, 'active_node').safeObjectOrEmpty)
const nodeType = computed(() => typy(node.value, 'type').safeString)
const specMap = computed(() => {
  switch (nodeType.value) {
    case NODE_TYPE_MAP.SCHEMA:
      return pickBy(INSIGHT_SPEC_MAP, (v, key) => key !== 'CREATION_INFO')
    case NODE_TYPE_MAP.TBL:
      return pick(INSIGHT_SPEC_MAP, ['COLUMNS', 'INDEXES', 'TRIGGERS', 'DDL'])
    case NODE_TYPE_MAP.VIEW:
    case NODE_TYPE_MAP.TRIGGER:
    case NODE_TYPE_MAP.SP:
    case NODE_TYPE_MAP.FN:
      return pick(INSIGHT_SPEC_MAP, ['CREATION_INFO', 'DDL'])
    default:
      return {}
  }
})
const tabItemDim = computed(() => ({
  width: props.dim.width,
  height: props.dim.height - tabNavHeight,
}))
const queryTabTmp = computed(() => QueryTabTmp.find(props.queryTab.id) || {})
const insight_data = computed(() => typy(queryTabTmp.value, 'insight_data').safeObjectOrEmpty)
const isSchemaNode = computed(() => nodeType.value === NODE_TYPE_MAP.SCHEMA)
const schemaName = computed(() => schemaNodeHelper.getSchemaName(node.value))
const specSqlMap = computed(() => {
  const { qualified_name } = node.value

  const nodeLiteralStr = `'${escapeSingleQuote(node.value.name)}'`

  const schemaIdentifier = quotingIdentifier(schemaName.value)
  const schemaLiteralStr = `'${escapeSingleQuote(schemaName.value)}'`

  const { CREATION_INFO, DDL, TABLES, VIEWS, COLUMNS, INDEXES, TRIGGERS, SP, FN } = INSIGHT_SPEC_MAP
  return Object.values(specMap.value).reduce((map, spec) => {
    switch (spec) {
      case CREATION_INFO:
      case DDL:
        map[spec] = `SHOW CREATE ${nodeType.value} ${qualified_name}`
        break
      case TABLES:
      case VIEWS:
        map[spec] = `SHOW TABLE STATUS FROM ${qualified_name} WHERE Comment ${
          spec === TABLES ? '<>' : '='
        } 'VIEW'`
        break
      case COLUMNS:
      case INDEXES: {
        const tbl =
          spec === COLUMNS ? 'INFORMATION_SCHEMA.COLUMNS' : 'INFORMATION_SCHEMA.STATISTICS'
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

watch(
  activeSpec,
  async (v) => {
    if (!insight_data.value[v]) await fetch(v)
  },
  { immediate: true }
)

async function fetch(spec) {
  if (queryTabConn.value.id && node.value.id) {
    const sql = specSqlMap.value[spec]
    if (sql) {
      const [, statementClasses] = getStatementClasses(sql)
      const [, statement] = enforceLimitOffset({
        statementClass: typy(statementClasses, '[0]').safeObject,
        limit: query_row_limit.value,
      })
      await queryResultService.queryInsightData({ connId: queryTabConn.value.id, statement, spec })
    }
  }
}
async function onReload({ statement, spec }) {
  await queryResultService.queryInsightData({ connId: queryTabConn.value.id, statement, spec })
}
</script>

<template>
  <VCard class="insight-viewer no-shadow fill-height" tile>
    <VTabs v-if="!$typy(specMap).isEmptyObject" v-model="activeSpec" :height="tabNavHeight">
      <VTab v-for="spec of specMap" :key="spec" :value="spec" class="text-primary">
        {{ spec }}
      </VTab>
    </VTabs>
    <KeepAlive v-for="(data, spec) in insight_data" :key="spec">
      <InsightViewerTabItem
        v-if="activeSpec === spec"
        :data="data"
        :spec="spec"
        :nodeType="nodeType"
        :dim="tabItemDim"
        :isSchemaNode="isSchemaNode"
        :style="{ height: `${tabItemDim.height}px` }"
        class="text-small-text"
        :onReload="async (statement) => await onReload({ statement, spec })"
      />
    </KeepAlive>
  </VCard>
</template>
