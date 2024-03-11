<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
defineProps({ servers: { type: Array, required: true } })
</script>

<template>
  <VTooltip location="top" :open-delay="200">
    <template v-for="(_, name) in $slots" #[name]="slotData">
      <slot :name="name" v-bind="slotData" />
    </template>
    <div v-for="server in servers" :key="server.id" class="d-flex align-center">
      <StatusIcon
        size="16"
        class="mr-1"
        :type="$typy(server, 'serverData.type').safeString"
        :value="$typy(server, 'serverData.attributes.state').safeString"
      />
      {{ server.id }}
      <span class="ml-1 text-text-subtle">
        {{ $t('uptime') }}
        {{ $helpers.uptimeHumanize($typy(server, 'serverData.attributes.uptime').safeNumber) }}
      </span>
    </div>
  </VTooltip>
</template>
