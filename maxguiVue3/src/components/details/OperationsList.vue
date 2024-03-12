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
defineProps({
  data: { type: Array, default: () => [] }, // 2d array
  handler: { type: Function, default: () => null },
})
</script>

<template>
  <VList v-for="(operations, i) in data" :key="i">
    <template v-for="op in operations">
      <VDivider v-if="op.divider" :key="`divider-${op.title}`" />
      <VListSubheader
        v-else-if="op.subheader"
        :key="op.subheader"
        class="pa-0 font-weight-medium"
        :title="op.subheader"
        :style="{ minHeight: '32px', paddingInlineStart: '8px !important' }"
      />
      <VListItem
        v-else
        :key="op.title"
        :title="op.title"
        link
        :disabled="op.disabled"
        class="px-2 py-0"
        :class="[`${op.type}-op`, op.disabled ? 'text-disabled' : '']"
        @click="handler(op)"
      >
        <template #prepend>
          <div class="d-inline-block text-center mr-2" style="width: 24px">
            <VIcon
              v-if="op.icon"
              :color="op.disabled ? '' : op.color"
              :size="op.iconSize"
              :icon="op.icon"
            />
          </div>
        </template>
      </VListItem>
    </template>
  </VList>
</template>
