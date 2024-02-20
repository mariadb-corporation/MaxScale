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
const props = defineProps({
  wrapperClass: { type: [String, Array, Object], default: () => '' },
  hoverableCard: { type: Boolean, default: false },
})
const emit = defineEmits(['is-hovered'])

let hover = ref(false)

function mouseHandler(e) {
  if (props.hoverableCard) {
    hover.value = e.type === 'mouseenter'
    emit('is-hovered', hover.value)
  }
}
</script>

<template>
  <div class="d-flex flex-column w-100 outlined-overview-card" :class="wrapperClass">
    <p class="title text-body-2 mb-3 text-uppercase font-weight-bold text-navigation">
      <slot name="title">
        <span :style="{ visibility: 'hidden' }" />
      </slot>
    </p>
    <VCard
      class="d-flex align-center justify-center flex-column card-ctr"
      :class="{ 'pointer card-ctr--hover': hover }"
      v-bind="$attrs"
      :height="$attrs.height || 75"
      flat
      border
      @mouseenter="mouseHandler"
      @mouseleave="mouseHandler"
    >
      <slot name="card-body"></slot>
    </VCard>
  </div>
</template>

<style lang="scss" scoped>
.outlined-overview-card {
  width: 100%;

  &:not(:first-of-type) {
    .card-ctr {
      border-left: none !important;
    }
  }
}

.card-ctr--hover {
  background-color: colors.$tr-hovered-color;
}
</style>
