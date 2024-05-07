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
  titleWrapperClass: { type: [String, Object, Array], default: '' },
  title: { type: String, required: true },
  titleInfo: [String, Number],
})
const isVisible = ref(true)
</script>

<template>
  <div class="collapsible-ctr">
    <div class="mb-1 d-flex align-center">
      <div class="d-flex align-center" :class="titleWrapperClass">
        <VBtn
          density="comfortable"
          variant="text"
          icon
          data-test="toggle-btn"
          class="arrow-toggle"
          @click="isVisible = !isVisible"
        >
          <VIcon
            :class="[isVisible ? 'rotate-down' : 'rotate-right']"
            icon="$mdiChevronDown"
            size="32"
            color="navigation"
          />
        </VBtn>
        <p
          class="collapsible-ctr-title mb-0 text-body-2 font-weight-bold text-navigation text-uppercase"
        >
          {{ title }}
          <span v-if="titleInfo || titleInfo === 0" class="ml-1 text-grayed-out">
            ({{ titleInfo }})
          </span>
        </p>
        <slot name="title-append" />
      </div>
      <v-spacer />
      <slot name="header-right" />
    </div>
    <VExpandTransition>
      <div v-show="isVisible" data-test="content" class="collapsible-ctr-content">
        <slot />
      </div>
    </VExpandTransition>
  </div>
</template>
