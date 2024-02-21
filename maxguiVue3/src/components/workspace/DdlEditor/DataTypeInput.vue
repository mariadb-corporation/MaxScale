<script>
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
export default {
  props: {
    modelValue: { type: String, required: true },
    items: { type: Array, default: () => [] },
  },
  computed: {
    inputValue: {
      get() {
        return this.modelValue
      },
      set(v) {
        if (v !== this.inputValue) this.$emit('update:modelValue', this.$typy(v).safeString)
      },
    },
  },
}
</script>

<template>
  <VCombobox
    v-model="inputValue"
    :items="items"
    item-props
    item-title="value"
    item-value="value"
    density="compact"
    hide-details
    :rules="[(v) => !!v]"
    :return-object="false"
  >
    <template #item="{ item, props }">
      <VDivider v-if="$typy(item.raw, 'divider').safeBoolean" v-bind="props" />
      <VListItem v-else v-bind="props" :title="item.value" :value="item.value" />
    </template>
  </VCombobox>
</template>
