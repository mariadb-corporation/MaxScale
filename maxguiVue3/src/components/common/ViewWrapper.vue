<script setup>
/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
defineProps({
  fluid: { type: Boolean, default: false },
  spacerStyle: { type: Object, default: () => {} },
  overflow: { type: Boolean, default: true },
})
</script>

<template>
  <div class="view-wrapper fill-height" :class="{ 'overflow-auto': overflow }">
    <div
      class="view-wrapper__container d-flex flex-column"
      :class="{ 'view-wrapper__container__fluid': fluid, 'fill-height': !overflow }"
    >
      <div class="view-header-container d-flex flex-wrap">
        <portal-target name="view-header__left" />
        <v-spacer :style="spacerStyle" />
        <portal-target name="view-header__right" />
      </div>
      <slot />
    </div>
  </div>
</template>

<style lang="scss" scoped>
//https://gs.statcounter.com/screen-resolution-stats top 2 for desktop screen resolution
.view-wrapper {
  padding: 24px 36px;
  .view-wrapper__container {
    max-width: 1366px; // preserve ratio for larger screen
    margin: 0 auto;
    &--fluid {
      max-width: 100%;
    }
  }
}
</style>
