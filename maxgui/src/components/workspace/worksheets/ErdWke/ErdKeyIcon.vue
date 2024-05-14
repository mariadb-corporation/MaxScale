<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
defineProps({ data: { type: Object, default: () => ({}) } })
</script>

<template>
  <span :style="{ width: '18px' }">
    <VIcon v-if="data" :size="data.size" :color="data.color" :icon="data.icon" />
  </span>
</template>
