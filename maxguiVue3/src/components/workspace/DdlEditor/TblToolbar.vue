<script>
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

export default {
  props: {
    selectedItems: { type: Array, required: true },
    isVertTable: { type: Boolean, default: true }, //sync
    showRotateTable: { type: Boolean, default: true },
    reverse: { type: Boolean, default: false },
  },
  computed: {
    isVertTableMode: {
      get() {
        return this.isVertTable
      },
      set(v) {
        this.$emit('update:isVertTable', v)
      },
    },
  },
  mounted() {
    this.$emit('get-computed-height', this.$refs.container.clientHeight)
  },
}
</script>

<template>
  <div
    ref="container"
    class="pb-2 d-flex align-center flex-1"
    :class="{ 'flex-row-reverse': reverse }"
  >
    <v-spacer />
    <TooltipBtn
      v-if="selectedItems.length"
      btnClass="delete-btn ml-2 px-1 text-capitalize"
      size="x-small"
      color="error"
      variant="outlined"
      @click="$emit('on-delete-selected-items', selectedItems)"
    >
      <template v-slot:btn-content>
        {{ $t('drop') }}
        <template v-if="selectedItems.length > 1"> ({{ selectedItems.length }}) </template>
      </template>
      {{ $t('dropSelected') }}
    </TooltipBtn>
    <VBtn
      class="add-btn ml-2 px-1 text-capitalize"
      size="x-small"
      color="primary"
      variant="outlined"
      @click="$emit('on-add')"
    >
      {{ $t('add') }}
    </VBtn>
    <TooltipBtn
      v-if="showRotateTable"
      btnClass="rotate-btn ml-2 px-1"
      size="x-small"
      color="primary"
      variant="outlined"
      @click="isVertTableMode = !isVertTableMode"
    >
      <template v-slot:btn-content>
        <VIcon size="14" icon="$mdiFormatRotate90" :class="{ 'rotate-left': !isVertTableMode }" />
      </template>
      {{ $t(isVertTableMode ? 'switchToHorizTable' : 'switchToVertTable') }}
    </TooltipBtn>
    <slot name="append" />
  </div>
</template>
