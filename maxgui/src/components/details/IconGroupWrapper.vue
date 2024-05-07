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

defineProps({ multiIcons: { type: Boolean, default: false } })
</script>

<template>
  <div class="d-inline-flex justify-center align-center icon-group-wrapper">
    <slot
      :props="{
        minWidth: 'unset',
        width: multiIcons ? '36px' : '42px',
        height: '36px',
        class: 'pa-0 rounded-0',
      }"
    />
  </div>
</template>

<style lang="scss" scoped>
.icon-group-wrapper:not(:last-of-type) {
  border-right: 1px solid colors.$grayed-out;
}
</style>
