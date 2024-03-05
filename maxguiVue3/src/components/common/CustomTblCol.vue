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
  name: { type: String, required: true },
  value: { type: [String, Array, Number, Boolean, Object], default: '' },
  search: { type: String, default: '' },
  autoTruncate: { type: Boolean, default: false },
})

const highlighter = computed(() => ({ keyword: props.search, txt: String(props.value) }))
const strValue = computed(() => String(props.value))
</script>

<template>
  <td class="v-data-table__td" :style="{ maxWidth: autoTruncate ? '1px' : 'unset' }">
    <slot :name="`item.${name}`" :value="value" :highlighter="highlighter">
      <GblTooltipActivator
        v-if="autoTruncate"
        :data="{ txt: strValue }"
        tag="div"
        :debounce="0"
        activateOnTruncation
        fillHeight
        v-mxs-highlighter="highlighter"
      />
      <span v-mxs-highlighter="highlighter" v-else>{{ strValue }}</span>
    </slot>
  </td>
</template>

<style lang="scss" scoped>
td {
  transition: background 0s !important;
}
</style>
