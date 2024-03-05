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
import RelationshipItems from '@/components/dashboard/RelationshipItems.vue'
import AnchorLink from '@/components/dashboard/AnchorLink.vue'

defineProps({
  value: { type: [String, Array, Number, Boolean, Object], default: '' },
  componentName: {
    type: String,
    required: true,
    validator: (v) => ['RelationshipItems', 'StatusIcon', 'AnchorLink'].includes(v),
  },
  objType: { type: String, required: true },
  mixTypes: { type: Boolean, default: false },
  highlighter: { type: Object, required: true },
  statusIconWithoutLabel: { type: Boolean, default: false },
})
</script>

<template>
  <RelationshipItems
    v-if="componentName === 'RelationshipItems'"
    :data="value"
    :type="objType"
    :highlighter="highlighter"
    :mixTypes="mixTypes"
  />
  <div v-else-if="componentName === 'StatusIcon'">
    <StatusIcon size="16" class="mr-1" :type="objType" :value="value" />
    <span v-if="!statusIconWithoutLabel" v-mxs-highlighter="highlighter"> {{ value }} </span>
  </div>

  <GblTooltipActivator
    v-else-if="componentName === 'AnchorLink'"
    :data="{ txt: value }"
    tag="div"
    :debounce="0"
    activateOnTruncation
    class="pointer"
    fillHeight
  >
    <AnchorLink
      v-mxs-highlighter="highlighter"
      :type="objType"
      :txt="value"
      :highlighter="highlighter"
    />
  </GblTooltipActivator>
</template>
