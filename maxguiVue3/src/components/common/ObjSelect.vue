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
/*
This component emits an event:
@has-changed: value = boolean
*/
export default {
  props: {
    modelValue: {
      validator: (v) => typeof v === 'object' || Array.isArray(v),
      default: () => [],
    },
    defaultItems: { type: [Array, Object], default: () => [] },
    items: { type: Array, required: true },
    entityName: { type: String, required: true },
    multiple: { type: Boolean, default: false },
    clearable: { type: Boolean, default: false },
    required: { type: Boolean, default: false },
    showPlaceHolder: { type: Boolean, default: true },
    errorMessages: { type: String, default: '' },
  },
  data() {
    return {
      rules: {
        requiredField: [(val) => this.validateRequired(val)],
      },
    }
  },
  computed: {
    // compare default value with new values
    hasChanged() {
      return !this.$helpers.lodash.isEqual(this.selectedItems, this.defaultItems)
    },
    selectedItems: {
      get() {
        return this.modelValue
      },
      set(v) {
        this.$emit('update:modelValue', v)
      },
    },
  },

  watch: {
    defaultItems: {
      deep: true,
      immediate: true,
      handler(v) {
        // Check for empty value, otherwise it will trigger input validation
        if (this.$typy(v).safeArray.length || !this.$typy(v).isEmptyObject) this.selectedItems = v
      },
    },
    hasChanged: {
      immediate: true,
      handler(v) {
        this.$emit('has-changed', v)
      },
    },
  },

  methods: {
    validateRequired(val) {
      // val is null when items are cleared by clearable props in VSelect
      if ((val === null || val.length === 0) && this.required) {
        return `${this.$t(this.entityName, this.multiple ? 2 : 1)} is required`
      }
      return true
    },
  },
}
</script>

<template>
  <VSelect
    v-model="selectedItems"
    :items="items"
    item-title="id"
    return-object
    :multiple="multiple"
    :name="entityName"
    :clearable="clearable"
    :placeholder="showPlaceHolder ? $t('select', [$t(entityName, multiple ? 2 : 1)]) : ''"
    :no-data-text="
      $t('noEntityAvailable', {
        entityName: $t(entityName, multiple ? 2 : 1),
      })
    "
    :rules="rules.requiredField"
    :hide-details="!required"
    :error-messages="errorMessages"
  >
    <template v-slot:selection="{ item, index }">
      <span v-if="index === 0" class="v-select__selection v-select__selection--comma">
        {{ item.id }}
      </span>
      <span
        v-if="index === 1"
        class="v-select__selection v-select__selection--comma mxs-color-helper text-caption text-grayed-out"
      >
        (+{{ selectedItems.length - 1 }} {{ $t('others') }})
      </span>
    </template>
  </VSelect>
</template>
