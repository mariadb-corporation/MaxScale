<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import AnchorLink from '@/components/dashboard/AnchorLink.vue'
defineOptions({
  inheritAttrs: false,
})
defineProps({
  data: { type: [Array, String], required: true },
  type: { type: String, default: '' },
  highlighter: { type: Object, required: true },
  // When data props is an array of object
  mixTypes: { type: Boolean, default: false },
})
</script>

<template>
  <span v-if="typeof data === 'string'" v-mxs-highlighter="highlighter" v-bind="$attrs">
    {{ data }}
  </span>
  <GblTooltipActivator
    v-else-if="data.length === 1"
    :data="{ txt: mixTypes ? data[0].id : data[0] }"
    tag="div"
    :debounce="0"
    activateOnTruncation
    class="pointer"
    fillHeight
    v-bind="$attrs"
  >
    <template #default="{ value }">
      <AnchorLink
        :type="mixTypes ? data[0].type : type"
        :txt="value"
        :highlighter="{ ...highlighter, txt: value }"
      />
    </template>
  </GblTooltipActivator>
  <VMenu
    v-else
    location="top"
    :close-on-content-click="false"
    open-on-hover
    content-class="shadow-drop text-navigation pa-4 text-body-2 bg-background rounded-10"
    min-width="1px"
  >
    <template #activator="{ props }">
      <div
        class="d-flex fill-height align-center pointer text-anchor"
        v-bind="{ ...props, ...$attrs }"
      >
        {{ data.length }}
        {{ $t(type, 2).toLowerCase() }}
      </div>
    </template>
    <div class="text-body-2">
      <AnchorLink
        v-for="(item, i) in data"
        :key="i"
        :type="mixTypes ? item.type : type"
        :txt="mixTypes ? item.id : item"
        class="d-block"
      />
    </div>
  </VMenu>
</template>
