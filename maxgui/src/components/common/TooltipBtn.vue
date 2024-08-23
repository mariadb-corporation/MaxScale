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
defineOptions({
  inheritAttrs: false,
})
defineProps({
  tooltipProps: { type: Object, default: () => ({}) },
  square: { type: Boolean, default: false },
})
</script>

<template>
  <VTooltip location="top" v-bind="tooltipProps">
    <template #activator="{ props }">
      <VBtn :class="{ 'square-btn': square }" v-bind="{ ...$attrs, ...props }">
        <slot name="btn-content" />
      </VBtn>
    </template>
    <span class="d-inline-block text-center text-capitalize">
      <slot />
    </span>
  </VTooltip>
</template>

<style lang="scss" scoped>
.square-btn {
  min-width: unset !important;
  padding: 0px !important;
  width: var(--v-btn-height) !important;
  height: var(--v-btn-height) !important;
  border-radius: 0px !important;
  &:hover {
    border-radius: 0px !important;
  }
}
</style>
