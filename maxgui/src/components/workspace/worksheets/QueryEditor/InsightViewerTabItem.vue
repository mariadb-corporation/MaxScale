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
import QueryResultTabWrapper from '@/components/workspace/worksheets/QueryEditor/QueryResultTabWrapper.vue'
import DataTable from '@/components/workspace/worksheets/QueryEditor/DataTable.vue'

import workspace from '@/composables/workspace'
import { NODE_TYPES, INSIGHT_SPECS } from '@/constants/workspace'
import { formatSQL } from '@/utils/queryUtils'

const props = defineProps({
  data: { type: Object, required: true },
  dim: { type: Object, required: true },
  spec: { type: String, required: true },
  nodeType: { type: String, required: true },
  isSchemaNode: { type: Boolean, required: true },
  onReload: { type: Function, required: true },
})

const typy = useTypy()

const specData = computed(() => props.data)
const { isLoading, startTime, execTime, endTime } = workspace.useCommonResSetAttrs(specData)
const resultset = computed(
  () => typy(specData.value, 'data.attributes.results[0]').safeObjectOrEmpty
)
const statement = computed(() => typy(resultset.value, 'statement').safeObject)
const ddl = computed(() => {
  let ddl = ''
  switch (props.nodeType) {
    case NODE_TYPES.TRIGGER:
    case NODE_TYPES.SP:
    case NODE_TYPES.FN:
      ddl = typy(resultset.value, `data[0][2]`).safeString
      break
    case NODE_TYPES.VIEW:
    default:
      ddl = typy(resultset.value, `data[0][1]`).safeString
  }
  return formatSQL(ddl)
})

const excludedColumnsBySpec = computed(() => {
  const { COLUMNS, INDEXES, TRIGGERS, SP, FN } = INSIGHT_SPECS
  const specs = [COLUMNS, INDEXES, TRIGGERS, SP, FN]
  let cols = ['TABLE_CATALOG', 'TABLE_SCHEMA']
  if (!props.isSchemaNode) cols.push('TABLE_NAME')
  return specs.reduce((map, spec) => {
    switch (spec) {
      case INDEXES:
        map[spec] = [...cols, 'INDEX_SCHEMA']
        break
      case TRIGGERS:
        map[spec] = props.isSchemaNode ? [] : ['Table']
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

const defHiddenHeaderIndexes = computed(() => {
  if (isFilteredSpec(props.spec))
    // plus 1 as DataTable automatically adds `#` column which is index 0
    return typy(resultset.value, 'fields').safeArray.reduce(
      (acc, field, index) =>
        excludedColumnsBySpec.value[props.spec].includes(field) ? [...acc, index + 1] : acc,
      []
    )
  return []
})

function isFilteredSpec(spec) {
  return Object.keys(excludedColumnsBySpec.value).includes(spec)
}
</script>

<template>
  <QueryResultTabWrapper
    :dim="dim"
    :isLoading="isLoading"
    showFooter
    :resInfoBarProps="{
      result: resultset,
      startTime,
      execTime,
      endTime,
    }"
  >
    <template #default="{ tblDim }">
      <SqlEditor
        v-if="spec === INSIGHT_SPECS.DDL"
        :modelValue="ddl"
        readOnly
        class="pt-2"
        :options="{ contextmenu: false, fontSize: 14 }"
        skipRegCompleters
        whiteBg
        :style="{ height: `${tblDim.height}px` }"
      />
      <DataTable
        v-else
        :data="resultset"
        :defHiddenHeaderIndexes="defHiddenHeaderIndexes"
        :height="tblDim.height"
        :width="tblDim.width"
        :hasInsertOpt="false"
        :toolbarProps="{ onReload, statement }"
      />
    </template>
  </QueryResultTabWrapper>
</template>
