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
import { VTooltip } from 'vuetify/components/VTooltip'
import { VMenu } from 'vuetify/components/VMenu'

const store = useStore()
const typy = useTypy()

const gbl_tooltip_data = computed(() => {
  return store.state.mxsApp.gbl_tooltip_data
})

const interactive = computed(() => typy(gbl_tooltip_data.value, 'interactive').safeBoolean)

const contentClass = computed(() => [
  'text-body-2',
  interactive.value ? 'py-2 px-4 interactive-tooltip' : '',
  typy(gbl_tooltip_data.value, 'contentClass').safeString,
])

const component = computed(() => (interactive.value ? VMenu : VTooltip))
</script>

<template>
  <component
    :is="component"
    v-if="gbl_tooltip_data"
    :key="gbl_tooltip_data.activatorID"
    :model-value="Boolean(gbl_tooltip_data)"
    open-on-hover
    :close-on-content-click="false"
    :location="$typy(gbl_tooltip_data, 'location').safeString || 'top'"
    :activator="`#${gbl_tooltip_data.activatorID}`"
    :transition="$typy(gbl_tooltip_data, 'transition').safeString || 'slide-y-transition'"
    content-class="shadow-drop rounded-sm"
    :max-width="$typy(gbl_tooltip_data, 'maxWidth').safeNumber || 800"
    :max-height="$typy(gbl_tooltip_data, 'maxHeight').safeNumber || 600"
  >
    <div
      :class="contentClass"
      :style="{
        whiteSpace: $typy(gbl_tooltip_data, 'whiteSpace').safeString || 'pre',
        wordWrap: $typy(gbl_tooltip_data, 'wordWrap').safeString || 'normal',
      }"
    >
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
  </component>
</template>

<style lang="scss" scoped>
.interactive-tooltip {
  background: vuetifyVar.$tooltip-background-color;
  opacity: 0.9;
  color: vuetifyVar.$tooltip-text-color;
  overflow: auto;
}
</style>
