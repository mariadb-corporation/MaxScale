<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
const props = defineProps({
  modelValue: { type: String, required: true },
  items: { type: Array, required: true },
  errTabId: { type: String, required: true },
})
const emit = defineEmits(['update:modelValue'])

let isOpened = ref(false)
let vListRef = ref(null)

let activeItem = computed({
  get: () => props.modelValue,
  set: (v) => emit('update:modelValue', v),
})
const activeItemIdx = computed(() => props.items.findIndex((item) => item === activeItem.value))

const isActiveTabErr = computed(() => activeItem.value === props.errTabId)

watch(isOpened, (v) => {
  if (v) nextTick(() => vListRef.value.scrollToIndex(activeItemIdx.value))
})

function onClickItem(item) {
  activeItem.value = item
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
        class="text-capitalize font-weight-medium pl-2 pr-3"
        :color="`${isActiveTabErr ? 'error' : 'primary'}`"
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
          class="pointer text-body-2"
          :class="[
            item === errTabId
              ? 'text-error'
              : item === activeItem
                ? 'text-primary'
                : 'text-navigation',
          ]"
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
