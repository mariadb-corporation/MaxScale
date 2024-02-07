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
import LoginForm from '@/components/login/LoginForm.vue'
import IntroAnimation from '@/components/login/IntroAnimation.vue'

const introAnimation = ref(null)
const typy = useTypy()

function onResize() {
  typy(introAnimation, 'value.onResize').safeFunction()
}
</script>

<template>
  <VContainer v-resize="onResize" fluid class="login-wrapper w-100 fill-height">
    <VRow>
      <VCol cols="12" class="d-flex flex-column align-center">
        <div class="logo mb-1 d-flex align-center justify-center">
          <img src="@/assets/logo.svg" alt="MariaDB Logo" />
          <span class="d-inline-block product-name font-weight-medium ml-2 text-white">
            MaxScale
          </span>
        </div>
        <LoginForm />
      </VCol>
      <IntroAnimation ref="introAnimation" />
    </VRow>
  </VContainer>
</template>

<style lang="scss" scoped>
.login-wrapper {
  background: radial-gradient(1100px at 100% 89%, colors.$accent 0%, colors.$deep-ocean 100%);
}
.logo {
  img {
    width: 265px;
    margin-bottom: 2px;
  }
  span {
    font-size: 31px;
  }
}
</style>
