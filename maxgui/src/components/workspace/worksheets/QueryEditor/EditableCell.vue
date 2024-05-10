<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
const props = defineProps({ data: { type: Object, required: true } })
const emit = defineEmits(['on-change'])

const {
  lodash: { isEqual, cloneDeep },
} = useHelpers()

const item = ref({})

const hasChanged = computed(() => !isEqual(item.value, props.data))

watch(
  () => props.data,
  (v, oV) => {
    if (!isEqual(v, oV)) item.value = cloneDeep(v)
  },
  { deep: true, immediate: true }
)
</script>

<template>
  <VTextField
    v-model.trim="item.value"
    class="fill-height align-center"
    density="compact"
    autocomplete="off"
    hide-details
    @update:modelValue="emit('on-change', { item, hasChanged })"
  />
</template>
