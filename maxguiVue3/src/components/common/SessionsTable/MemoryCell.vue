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
defineOptions({ inheritAttrs: false })
defineProps({ data: { type: Object, required: true } })
</script>

<template>
  <VMenu
    location="right"
    transition="slide-y-transition"
    :close-on-content-click="false"
    open-on-hover
    content-class="shadow-drop text-navigation pa-4 text-body-2 bg-background rounded-10"
  >
    <template #activator="{ props }">
      <div
        class="pointer d-flex override-td--padding fill-height align-center"
        v-bind="{ ...props, ...$attrs }"
      >
        {{ data.total }}
      </div>
    </template>
    <div class="text-body-2">
      <TreeTable :data="data" hideHeader expandAll density="compact" class="my-2" />
    </div>
  </VMenu>
</template>
