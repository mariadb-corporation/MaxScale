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
defineProps({ defItem: { type: String, required: true } })
</script>

<template>
  <VCombobox hide-details="auto">
    <template #item="{ props }">
      <VListItem v-bind="props">
        <template #title="{ title }">
          {{ title }}
          <span class="ml-1"> {{ title === defItem ? `(${$t('def')})` : '' }}</span>
        </template>
      </VListItem>
    </template>
  </VCombobox>
</template>
