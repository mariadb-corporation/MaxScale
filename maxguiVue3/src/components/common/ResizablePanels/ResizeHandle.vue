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
  split: { validator: (value) => ['vert', 'horiz'].indexOf(value) !== -1, required: true },
  active: { type: Boolean, required: true },
})
</script>

<template>
  <div
    class="resize-handle"
    :class="[
      `resize-handle--${split}`,
      `resize-handle--${split}-${active ? 'resizing' : 'not-resizing'}`,
    ]"
  />
</template>

<style lang="scss" scoped>
.resize-handle {
  position: absolute;
  opacity: 0.5;
  z-index: 1;
  &--horiz {
    height: 11px;
    margin: -5px 0;
    border-top: 5px solid transparent;
    border-bottom: 5px solid transparent;
    cursor: row-resize;
    width: 100%;
    &-resizing,
    &:hover {
      border-top: 5px solid colors.$table-border;
      border-bottom: 5px solid colors.$table-border;
    }
  }
  &--vert {
    width: 11px;
    height: 100%;
    margin-left: -5px;
    border-left: 5px solid transparent;
    border-right: 5px solid transparent;
    cursor: col-resize;
    &-resizing,
    &:hover {
      border-left: 5px solid colors.$table-border;
      border-right: 5px solid colors.$table-border;
    }
  }
}
</style>
