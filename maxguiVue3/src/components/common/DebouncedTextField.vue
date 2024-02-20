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
defineOptions({
  inheritAttrs: false,
})
const attrs = useAttrs()
const props = defineProps({
  debounceTime: { type: Number, default: 500 },
})
const emit = defineEmits(['update:modelValue'])

const modelValue = computed(() => attrs.modelValue)

const {
  lodash: { debounce },
} = useHelpers()

let inputValue = ref(attrs.modelValue)

let debouncedInput = debounce((v) => {
  inputValue.value = v
  emit('update:modelValue', v)
}, props.debounceTime)

watch(modelValue, (v) => {
  if (v !== inputValue.value) inputValue.value = v
})
defineExpose({ debouncedInput, inputValue })
</script>

<template>
  <VTextField v-bind="$attrs" :value="inputValue" @update:modelValue="debouncedInput" />
</template>
