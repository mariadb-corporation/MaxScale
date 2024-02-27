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
  columns: { type: Array, required: true },
  index: { type: Number },
  name: { type: String, required: true },
  value: { type: [String, Array, Number, Boolean], default: '' },
  search: { type: String, default: '' },
  autoTruncate: { type: Boolean, default: false },
})

const highlighter = computed(() => ({ keyword: props.search, txt: `${props.value}` }))
const slotName = computed(() => `item.${props.name}`)
</script>

<template>
  <td
    class="v-data-table__td v-data-table-column--align-start"
    :style="{
      maxWidth: autoTruncate ? '1px' : 'unset',
    }"
    v-bind="columns[index].cellProps"
  >
    <slot :name="slotName" :value="value" :highlighter="highlighter">
      <GblTooltipActivator
        v-if="autoTruncate"
        v-mxs-highlighter="highlighter"
        :data="{ txt: String(value) }"
        tag="div"
        :debounce="0"
        activateOnTruncation
        fillHeight
      />
      <span v-else v-mxs-highlighter="highlighter">{{ String(value) }}</span>
    </slot>
  </td>
</template>

<style lang="scss" scoped>
td {
  transition: background 0s !important;
}
</style>
