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
const props = defineProps({
  errResPrefix: { type: String, required: true },
  queryCanceledPrefix: { type: String, required: true },
})

const attrs = useAttrs()

const activeItemColor = computed(() => colorize(attrs.modelValue))

function colorize(item) {
  if (item.includes(props.errResPrefix)) return 'error'
  else if (item.includes(props.queryCanceledPrefix)) return 'warning'
  else if (item === attrs.modelValue) return 'primary'
  return 'navigation'
}
</script>

<template>
  <VSelect
    class="result-set-items minimized-input flex-grow-0"
    :class="[
      `text-${activeItemColor}`,
      activeItemColor === 'error' || activeItemColor === 'warning' ? '' : 'borderless-input',
    ]"
    hide-details
    :color="activeItemColor"
    :base-color="activeItemColor"
  >
    <template #item="{ props, item }">
      <VListItem v-bind="props" :class="`text-${colorize(item.value)}`" />
    </template>
  </VSelect>
</template>

<style lang="scss" scoped>
.result-set-items {
  :deep(.v-input__control) {
    .v-field__input {
      padding: 0 0 0 8px !important;
      color: inherit !important;
    }
  }
}
</style>
