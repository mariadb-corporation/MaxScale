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
defineOptions({
  inheritAttrs: false,
})
defineProps({
  tooltipProps: { type: Object, default: () => ({}) },
})
</script>

<template>
  <VTooltip location="top" transition="slide-y-transition" v-bind="tooltipProps">
    <template #activator="{ props }">
      <VBtn v-bind="{ ...$attrs, ...props }">
        <slot name="btn-content" />
      </VBtn>
    </template>
    <span class="d-inline-block text-center text-capitalize">
      <slot />
    </span>
  </VTooltip>
</template>
