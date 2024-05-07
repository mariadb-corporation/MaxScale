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
import ResultDataTable from '@wkeComps/QueryEditor/ResultDataTable.vue'

const props = defineProps({
  data: { type: Object, required: true },
  height: { type: Number, required: true },
  width: { type: Number, required: true },
  resultDataTableProps: { type: Object, default: () => ({}) },
  customHeaders: { type: Array, default: () => [] },
})

const typy = useTypy()

const tableData = computed(() => ({
  headers: props.customHeaders.length
    ? props.customHeaders
    : typy(props.data, 'fields').safeArray.map((field) => ({ text: field })),
  rows: typy(props.data, 'data').safeArray,
  metadata: typy(props.data, 'metadata').safeArray,
  complete: typy(props.data, 'complete').safeBoolean,
}))
</script>

<template>
  <ResultDataTable
    v-if="$typy(data, 'fields').isDefined"
    :height="height"
    :width="width"
    :headers="tableData.headers"
    :data="tableData.rows"
    :metadata="tableData.metadata"
    v-bind="resultDataTableProps"
  >
    <template v-for="(_, name) in $slots" #[name]="slotData">
      <slot :name="name" v-bind="slotData" />
    </template>
  </ResultDataTable>
  <div v-else :style="{ height: `${height}px` }">
    <div v-for="(v, key) in data" :key="key">
      <b>{{ key }}:</b>
      <span class="d-inline-block ml-4">{{ v }}</span>
    </div>
  </div>
</template>
