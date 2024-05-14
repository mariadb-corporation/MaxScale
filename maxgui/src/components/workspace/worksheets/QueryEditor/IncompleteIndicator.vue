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
defineProps({ resSet: { type: Object, required: true } })
</script>

<template>
  <VTooltip v-if="$typy(resSet, 'fields').isDefined && !resSet.complete" location="top">
    <template #activator="{ props }">
      <div class="ml-4 d-flex align-center" v-bind="props">
        <VIcon size="16" color="error" class="mr-2" icon="mxs:alertWarning" />
        {{ $t('incomplete') }}
      </div>
    </template>
    {{ $t('info.queryIncomplete') }}
  </VTooltip>
</template>
