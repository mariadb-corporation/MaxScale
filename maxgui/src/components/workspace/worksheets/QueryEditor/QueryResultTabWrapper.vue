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
import ResInfoBar from '@wkeComps/QueryEditor/ResInfoBar.vue'

const props = defineProps({
  dim: { type: Object, required: true },
  isLoading: { type: Boolean, default: false },
  showFooter: { type: Boolean, required: true },
  resInfoBarProps: { type: Object, default: () => ({}) },
})

const typy = useTypy()

const INFO_BAR_HEIGHT = 24

const tblDim = computed(() => ({
  width: props.dim.width - 40, //  Minus 40px which is the horizontal padding px-5
  height: typy(props, 'dim.height').safeNumber - 8 - (props.showFooter ? INFO_BAR_HEIGHT : 0), // minus 8px to have a vertical divider space
}))
</script>

<template>
  <div class="query-result-tab-wrapper pos--relative">
    <div class="px-5 d-flex flex-column">
      <VProgressLinear v-if="isLoading" indeterminate color="primary" class="mt-2" />
      <slot v-else :tblDim="tblDim" />
    </div>
    <ResInfoBar
      v-if="showFooter"
      :isLoading="isLoading"
      :height="INFO_BAR_HEIGHT"
      :width="dim.width"
      v-bind="resInfoBarProps"
      class="res-info-bar px-5 pos--absolute"
    />
  </div>
</template>

<style lang="scss" scoped>
.res-info-bar {
  bottom: 0;
}
</style>
