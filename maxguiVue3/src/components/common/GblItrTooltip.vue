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
 *  Public License.
 */
const store = useStore()
const typy = useTypy()

const gbl_tooltip_data = computed(() => {
  return store.state.mxsApp.gbl_tooltip_data
})
const contentClass = computed(() => [
  'tooltip-content py-2 px-4 text-body-2',
  typy(gbl_tooltip_data.value, 'contentClass').safeString,
])
</script>

<template>
  <VMenu
    v-if="gbl_tooltip_data"
    :key="gbl_tooltip_data.activatorID"
    :model-value="Boolean(gbl_tooltip_data)"
    open-on-hover
    :close-on-content-click="false"
    :location="typy(gbl_tooltip_data, 'location').safeString || 'top'"
    :activator="`#${gbl_tooltip_data.activatorID}`"
    transition="slide-y-transition"
    content-class="shadow-drop rounded-sm"
    :max-width="typy(gbl_tooltip_data, 'maxWidth').safeNumber || 800"
    :max-height="typy(gbl_tooltip_data, 'maxHeight').safeNumber || 600"
  >
    <div :class="contentClass">
      <template v-if="!$typy(gbl_tooltip_data, 'collection').isUndefined">
        <span
          v-for="(value, key) in gbl_tooltip_data.collection"
          :key="key"
          class="d-block text-body-2"
        >
          <span class="mr-1 font-weight-bold text-capitalize"> {{ key }}: </span>
          <span> {{ value }}</span>
        </span>
      </template>
      <template v-else>
        {{ gbl_tooltip_data.txt }}
      </template>
    </div>
  </VMenu>
</template>

<style lang="scss" scoped>
.tooltip-content {
  background: vuetifyVar.$tooltip-background-color;
  opacity: 0.9;
  color: vuetifyVar.$tooltip-text-color;
  white-space: pre;
  overflow: auto;
}
</style>
