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

const props = defineProps({
  selectedItems: { type: Array, required: true },
  isVertTable: { type: Boolean, default: true }, //sync
  showRotateTable: { type: Boolean, default: true },
  reverse: { type: Boolean, default: false },
})
const emit = defineEmits(['update:isVertTable', 'get-computed-height', 'on-add', 'on-delete'])

const containerRef = ref(null)

const isVertTableMode = computed({
  get: () => props.isVertTable,
  set: (v) => emit('update:isVertTable', v),
})
onMounted(() => emit('get-computed-height', containerRef.value.clientHeight))
</script>

<template>
  <div
    ref="containerRef"
    class="pb-2 d-flex align-center flex-1"
    :class="{ 'flex-row-reverse': reverse }"
  >
    <VSpacer />
    <TooltipBtn
      v-if="selectedItems.length"
      data-test="delete-btn"
      class="ml-2 px-1 text-capitalize font-weight-medium"
      color="error"
      variant="outlined"
      density="comfortable"
      size="small"
      @click="emit('on-delete')"
    >
      <template #btn-content>
        {{ $t('drop') }}
        <template v-if="selectedItems.length > 1"> ({{ selectedItems.length }}) </template>
      </template>
      {{ $t('dropSelected') }}
    </TooltipBtn>
    <VBtn
      data-test="add-btn"
      class="ml-2 px-1 text-capitalize"
      size="small"
      :width="36"
      min-width="unset"
      color="primary"
      variant="outlined"
      density="comfortable"
      @click="emit('on-add')"
    >
      {{ $t('add') }}
    </VBtn>
    <TooltipBtn
      v-if="showRotateTable"
      data-test="rotate-btn"
      class="ml-2 px-1"
      size="small"
      :width="36"
      min-width="unset"
      density="comfortable"
      color="primary"
      variant="outlined"
      @click="isVertTableMode = !isVertTableMode"
    >
      <template #btn-content>
        <VIcon size="14" icon="$mdiFormatRotate90" :class="{ 'rotate-left': !isVertTableMode }" />
      </template>
      {{ $t(isVertTableMode ? 'switchToHorizTable' : 'switchToVertTable') }}
    </TooltipBtn>
    <slot name="append" />
  </div>
</template>
