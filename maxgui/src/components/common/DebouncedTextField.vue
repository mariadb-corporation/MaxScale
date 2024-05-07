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
const props = defineProps({ debounceTime: { type: Number, default: 500 } })
const emit = defineEmits(['update:modelValue'])

const {
  lodash: { debounce },
} = useHelpers()

let debouncedInput = debounce((v) => {
  emit('update:modelValue', v)
}, props.debounceTime)
</script>

<template>
  <VTextField @update:modelValue="debouncedInput" />
</template>
