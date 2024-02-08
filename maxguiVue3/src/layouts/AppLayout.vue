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
import AppSidebar from '@/layouts/AppSidebar.vue'
import AppBar from '@/layouts/AppBar.vue'
import SnackbarMsg from '@/components/SnackbarMsg.vue'
</script>

<template>
  <VLayout full-height>
    <AppBar />
    <AppSidebar />
    <VMain>
      <RouterView v-slot="{ Component }">
        <transition name="fade" mode="out-in">
          <component :is="Component"></component>
        </transition>
      </RouterView>
    </VMain>
    <VFooter
      class="pl-2 pr-0 d-flex mxs-color-helper white border-top-separator justify-center"
      padless
      app
      height="40"
      inset
    >
      <span class="footer-text text-code-color align-center text-truncate">
        {{ $t('mariadbCopyright') }}
        <span class="footer__separator" />
        <a
          target="_blank"
          rel="noopener noreferrer"
          href="https://mariadb.com/privacy-policy/"
          class="rsrc-link"
        >
          {{ $t('privacyPolicy') }}
        </a>
        <span class="footer__separator" />
        <a
          target="_blank"
          rel="noopener noreferrer"
          href="https://mariadb.com/product-terms-condition/"
          class="rsrc-link"
        >
          {{ $t('termsOfUse') }}
        </a>
      </span>
    </VFooter>
    <SnackbarMsg />
  </VLayout>
</template>

<style lang="scss" scoped>
.footer-text {
  font-size: 0.625rem;
  a {
    text-decoration: none;
  }
}

.fade-enter-active,
.fade-leave-active {
  height: 100%;
  transition-duration: 0.2s;
  transition-property: opacity;
  transition-timing-function: ease;
}

.fade-enter-from,
.fade-leave-to {
  opacity: 0;
}

.footer__separator::after {
  content: '|';
  margin: 0 10px;
}
</style>
