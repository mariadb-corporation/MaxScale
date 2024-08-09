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
import resultSetExtractor from '@/utils/resultSetExtractor'
import { INSIGHT_SPEC_MAP } from '@/constants/workspace'

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
const resultSet = computed(
  () => typy(specData.value, 'data.attributes.results[0]').safeObjectOrEmpty
)
const statement = computed(() => typy(resultSet.value, 'statement').safeObject)
const ddl = computed(() =>
  resultSetExtractor.getDdl({ type: props.nodeType, resultSet: resultSet.value })
)
const excludedColumnsBySpec = computed(() => {
  const { COLUMNS, INDEXES, TRIGGERS, SP, FN } = INSIGHT_SPEC_MAP
  const specs = [COLUMNS, INDEXES, TRIGGERS, SP, FN]
  const cols = ['TABLE_CATALOG', 'TABLE_SCHEMA']
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
    return typy(resultSet.value, 'fields').safeArray.reduce(
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
      result: resultSet,
      startTime,
      execTime,
      endTime,
    }"
  >
    <template #default="{ tblDim }">
      <SqlEditor
        v-if="spec === INSIGHT_SPEC_MAP.DDL"
        :modelValue="ddl"
        readOnly
        class="pt-2 mx-n5"
        :options="{ contextmenu: false, fontSize: 14 }"
        skipRegCompleters
        whiteBg
        :style="{ height: `${tblDim.height}px` }"
      />
      <DataTable
        v-else
        :data="resultSet"
        :defHiddenHeaderIndexes="defHiddenHeaderIndexes"
        :height="tblDim.height"
        :width="tblDim.width"
        :hasInsertOpt="false"
        :toolbarProps="{ onReload, statement }"
      />
    </template>
  </QueryResultTabWrapper>
</template>
