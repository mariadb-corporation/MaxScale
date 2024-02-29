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
defineProps({
  type: { type: String, required: true },
  clearable: { type: Boolean, default: false },
  multiple: { type: Boolean, default: false },
  items: { type: Array, required: true },
  initialValue: { type: [Array, Object], default: () => [] },
})
const emit = defineEmits(['on-open', 'selected-items'])
const attrs = useAttrs()
const typy = useTypy()
let selectedItems = ref([])
let hasChanged = ref(false)

watch(
  () => attrs.modelValue,
  (v) => {
    if (v) emit('on-open')
    else selectedItems.value = []
  }
)

watch(
  selectedItems,
  (v) => {
    if (typy(v).isNull) emit('selected-items', [])
    else if (typy(v).isArray) emit('selected-items', v)
    else emit('selected-items', [v])
  },
  { deep: true }
)
</script>

<template>
  <BaseDlg :hasChanged="hasChanged">
    <template #form-body>
      <label class="field__label text-small-text d-block" data-test="select-label" for="obj-select">
        {{ $t('specify', multiple ? 2 : 1) }}
        {{ $t(type, multiple ? 2 : 1) }}
      </label>
      <ObjSelect
        v-model="selectedItems"
        :initialValue="initialValue"
        :items="items"
        :type="type"
        :multiple="multiple"
        :clearable="clearable"
        :showPlaceHolder="false"
        id="obj-select"
        @has-changed="hasChanged = $event"
      />
      <slot name="body-append" />
    </template>
  </BaseDlg>
</template>
