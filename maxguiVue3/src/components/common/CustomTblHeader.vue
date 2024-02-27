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

/*
 * A simplified version of Vuetify VDataTableHeader
 */
const props = defineProps({
  column: { type: Object, required: true },
  sortBy: { type: Object, required: true },
  total: { type: Number },
  showTotal: { type: Boolean, default: false },
})

const isDesc = computed(() => props.sortBy.isDesc)
const isSorted = computed(() => props.sortBy.key === props.column.value)
</script>

<template>
  <th
    class="v-data-table__td v-data-table-column--align-start v-data-table__th"
    :class="{ 'v-data-table__th--sorted': isSorted }"
    v-bind="column.headerProps"
  >
    <div class="v-data-table-header__content">
      {{ column.title }}
      <span v-if="showTotal && $typy(total).isDefined" class="ml-1 total" data-test="total-count">
        ({{ total }})
      </span>
      <VIcon
        icon="mxs:arrowDown"
        size="14"
        class="ml-3 sort-icon"
        data-test="sort-icon"
        :class="{ 'sort-icon--desc': isDesc }"
      />
    </div>
  </th>
</template>

<style lang="scss" scoped>
th {
  cursor: pointer;
  &:hover,
  &.v-data-table__th--sorted {
    .v-data-table-header__content {
      .sort-icon {
        opacity: 1;
        visibility: visible;
      }
    }
  }
  &.v-data-table__th--sorted {
    .v-data-table-header__content {
      .total {
        color: black;
      }
      .sort-icon--desc {
        transform: rotate(-180deg);
        transform-origin: center;
      }
    }
  }
  .v-data-table-header__content {
    .total {
      color: colors.$grayed-out;
    }
    .sort-icon {
      visibility: hidden;
      opacity: 0;
    }
  }
}
</style>
