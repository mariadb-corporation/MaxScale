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
import Worksheet from '@wsModels/Worksheet'
import WkeNavTab from '@wsComps/WkeNavTab.vue'
import WkeToolbar from '@wsComps/WkeToolbar.vue'

defineProps({ height: { type: Number, required: true } })

let pageToolbarBtnWidth = ref(128)
const activeWkeID = computed({
  get() {
    return Worksheet.getters('activeId')
  },
  set(v) {
    if (v) Worksheet.commit((state) => (state.active_wke_id = v))
  },
})
const allWorksheets = computed(() => Worksheet.all())
</script>

<template>
  <div class="d-flex">
    <VTabs
      v-model="activeWkeID"
      show-arrows
      hide-slider
      :height="height"
      class="workspace-tab-style wke-tab-nav pagination-btn--small flex-grow-0"
      :style="{ maxWidth: `calc(100% - ${pageToolbarBtnWidth + 1}px)` }"
      center-active
    >
      <VTab
        v-for="wke in allWorksheets"
        :key="wke.id"
        :value="wke.id"
        class="pa-0"
        selected-class="v-tab--selected text-primary"
      >
        <WkeNavTab :wke="wke" />
      </VTab>
    </VTabs>
    <WkeToolbar
      @get-total-btn-width="pageToolbarBtnWidth = $event"
      class="flex-grow-1"
      :style="{ height: `${height}px` }"
    />
  </div>
</template>
