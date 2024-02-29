<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
const props = defineProps({
  data: { type: Array, required: true },
  totalMap: { type: Object, default: () => ({}) },
})

const store = useStore()
const typy = useTypy()
const {
  lodash: { cloneDeep },
} = useHelpers()
const { sortBy, toggleSortBy, compareFn } = useSortBy({ key: 'id', isDesc: false })

const search_keyword = computed(() => store.state.search_keyword)

const items = computed(() => {
  let data = cloneDeep(props.data)
  if (sortBy.value.key) data.sort(compareFn)
  return data
})
</script>

<template>
  <VDataTable :items="items" :search="search_keyword" :itemsPerPage="-1">
    <template #headers="{ columns }">
      <tr>
        <template v-for="column in columns" :key="column.value">
          <CustomTblHeader
            :column="column"
            :sortBy="sortBy"
            :total="totalMap[column.value]"
            :showTotal="typy(totalMap[column.value]).isDefined"
            @click="toggleSortBy(column.value)"
          />
        </template>
      </tr>
    </template>
    <template #item="{ item, columns }">
      <tr class="v-data-table__tr">
        <CustomTblCol
          v-for="(h, i) in columns"
          :key="h.value"
          :value="item[h.value]"
          :name="h.value"
          :search="search_keyword"
          :autoTruncate="h.autoTruncate"
          :class="{
            'text-no-wrap': typy(h.customRender, 'renderer').safeString === 'StatusIcon',
          }"
          v-bind="columns[i].cellProps"
        >
          <template v-if="h.customRender" #[`item.${h.value}`]="{ value, highlighter }">
            <CustomCellRenderer
              :value="value"
              :componentName="h.customRender.renderer"
              :objType="h.customRender.objType"
              :mixTypes="typy(h.customRender, 'mixTypes').safeBoolean"
              :highlighter="highlighter"
              v-bind="h.customRender.props"
            />
          </template>
        </CustomTblCol>
      </tr>
    </template>
    <template #bottom />
  </VDataTable>
</template>
