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
  split: { type: String, required: true },
  isLeft: { type: Boolean, default: false },
})
const splitTypeClass = computed(() => `resizable-pane--${props.split}`)
const panePosClass = computed(
  () => `${props.isLeft ? `${splitTypeClass.value}-left` : `${splitTypeClass.value}-right`}`
)
</script>

<template>
  <div :class="`resizable-pane ${splitTypeClass} ${panePosClass}`">
    <slot />
  </div>
</template>

<style lang="scss" scoped>
.resizable-pane {
  position: absolute;
  overflow: hidden;
  &--vert {
    height: 100%;
    &-left {
      left: 0;
    }
    &-right {
      right: 0;
    }
  }

  &--horiz {
    width: 100%;
    &-left {
      top: 0;
    }
    &-right {
      bottom: 0;
    }
  }
}
</style>
