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
const store = useStore()
const isAdmin = computed(() => store.getters['users/isAdmin'])
const goBack = useGoBack()
</script>
<template>
  <portal to="view-header__left">
    <div class="d-flex align-center">
      <VBtn class="ml-n4" icon variant="text" density="comfortable" @click="goBack">
        <VIcon
          class="mr-1"
          style="transform: rotate(90deg)"
          size="28"
          color="navigation"
          icon="mxs:arrowDown"
        />
      </VBtn>
      <div class="d-inline-flex align-center">
        <GblTooltipActivator
          :data="{ txt: `${$route.params.id}` }"
          :maxWidth="300"
          activateOnTruncation
        >
          <span class="ml-1 mb-0 text-navigation text-h4 page-title">
            <slot name="page-title" :pageId="$route.params.id">
              {{ $route.params.id }}
            </slot>
          </span>
        </GblTooltipActivator>

        <VMenu
          v-if="isAdmin && $slots['setting-menu']"
          content-class="full-border rounded bg-background"
          transition="slide-y-transition"
          offset="4"
        >
          <template #activator="{ props }">
            <VBtn class="ml-2 gear-btn" icon variant="text" density="comfortable" v-bind="props">
              <VIcon size="18" color="primary" icon="mxs:settings" />
            </VBtn>
          </template>
          <div v-if="$slots['setting-menu']" class="d-inline-flex">
            <slot name="setting-menu"></slot>
          </div>
        </VMenu>
      </div>
    </div>
  </portal>
</template>

<style lang="scss" scoped>
.page-title {
  line-height: normal;
}
</style>
