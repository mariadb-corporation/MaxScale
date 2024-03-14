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
</script>

<template>
  <VSelect hide-details>
    <template #item="{ props }">
      <VListItem v-bind="props">
        <template #title="{ title }">
          <span v-if="title" class="text-capitalize"> {{ $t(title, 1) }}</span>
        </template>
      </VListItem>
    </template>
    <template #selection="{ item }">
      <span v-if="item.title" class="font-weight-bold text-capitalize">
        {{ $t(item.title, 1) }}
      </span>
    </template>
  </VSelect>
</template>
