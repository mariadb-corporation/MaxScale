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
  className: { type: [String, Array, Object], default: 'pt-4 pr-4 pb-8 pl-8' },
  headerClassName: { type: [String, Array, Object], default: 'px-3 pt-2' },
  bodyClassName: { type: [String, Array, Object], default: '' },
  footerClassName: { type: [String, Array, Object], default: 'px-3' },
})
</script>

<template>
  <div class="wizard-stage-ctr d-flex flex-column fill-height" :class="className">
    <div v-if="$slots['header']" class="wizard-stage-ctr__header d-flex" :class="headerClassName">
      <slot name="header" />
    </div>
    <div v-if="$slots['body']" class="wizard-stage-ctr__body" :class="bodyClassName">
      <slot name="body" />
    </div>
    <div
      v-if="$slots['footer']"
      class="wizard-stage-ctr__footer d-flex align-end"
      :class="footerClassName"
    >
      <slot name="footer" />
    </div>
  </div>
</template>

<style lang="scss">
.wizard-stage-ctr {
  position: absolute;
  top: 0;
  left: 0;
  width: 100%;
  &__body {
    flex: 1;
    overflow-y: auto;
  }
  .mxs-stage-ctr__title {
    line-height: 36px;
  }
}
</style>
