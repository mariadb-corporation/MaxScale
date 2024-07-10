<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2029-02-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
const props = defineProps({
  modelValue: { type: String, required: true },
  items: { type: Array, required: true },
  errResPrefix: { type: String, required: true },
  queryCanceledPrefix: { type: String, required: true },
})
const emit = defineEmits(['update:modelValue'])

const isOpened = ref(false)
const vListRef = ref(null)

const activeItem = computed({
  get: () => props.modelValue,
  set: (v) => emit('update:modelValue', v),
})
const activeItemIdx = computed(() => props.items.findIndex((item) => item === activeItem.value))

watch(isOpened, (v) => {
  if (v) nextTick(() => vListRef.value.scrollToIndex(activeItemIdx.value))
})

function onClickItem(item) {
  activeItem.value = item
}

function colorize(item) {
  if (item.includes(props.errResPrefix)) return 'error'
  else if (item.includes(props.queryCanceledPrefix)) return 'warning'
  else if (item === activeItem.value) return 'primary'
  return 'navigation'
}
</script>

<template>
  <VMenu
    v-model="isOpened"
    transition="slide-y-transition"
    location="bottom"
    content-class="full-border"
    close-on-content-click
    :maxHeight="300"
  >
    <template #activator="{ props, isActive }">
      <VBtn
        size="small"
        variant="outlined"
        density="comfortable"
        class="text-capitalize font-weight-medium px-2"
        :color="`${colorize(activeItem)}`"
        tile
        v-bind="props"
      >
        {{ activeItem }}
        <template #append>
          <VIcon size="10" :class="[isActive ? 'rotate-up' : 'rotate-down']" icon="mxs:menuDown" />
        </template>
      </VBtn>
    </template>
    <VVirtualScroll ref="vListRef" :items="items" :item-height="32" class="bg-white">
      <template #default="{ item }">
        <VListItem
          class="text-body-2"
          :class="[`text-${colorize(item)}`]"
          density="compact"
          :active="item === activeItem"
          color="primary"
          @click="onClickItem(item)"
        >
          {{ item }}
        </VListItem>
      </template>
    </VVirtualScroll>
  </VMenu>
</template>
