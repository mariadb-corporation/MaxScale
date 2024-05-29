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
import usersService from '@/services/usersService'

const store = useStore()
const { t } = useI18n()

const login_err_msg = computed(() => store.state.users.login_err_msg)
const formValidity = ref(null)
const isLoading = ref(false)
const isPwdVisible = ref(false)
const rememberMe = ref(true)
const credential = ref({ username: '', password: '' })
const rules = {
  username: [(v) => !!v || t('errors.requiredInput', { inputName: 'Username' })],
  password: [(v) => !!v || t('errors.requiredInput', { inputName: 'Password' })],
}

function onInput() {
  if (login_err_msg.value) store.commit('users/SET_LOGIN_ERR_MSG', '')
}
async function handleLogin() {
  isLoading.value = true
  await usersService.login({ rememberMe: rememberMe.value, auth: credential.value })
  isLoading.value = false
}
</script>
<template>
  <VCard
    :min-width="463"
    style="z-index: 2; border-radius: 10px"
    elevation="5"
    class="px-10 pb-3"
    tabindex="0"
    @keyup.enter="formValidity && handleLogin()"
  >
    <VCardText class="pt-15 px-10 pb-0">
      <h1 class="pb-4 text-h1 text-left text-deep-ocean">
        {{ $t('welcome') }}
      </h1>
      <VForm ref="form" v-model="formValidity" validate-on="input lazy" class="pt-4">
        <VTextField
          v-model="credential.username"
          :rules="rules.username"
          :error-messages="login_err_msg"
          class="mt-5 v-text-field--message-up"
          name="username"
          autocomplete="username"
          autofocus
          dense
          :height="36"
          single-line
          outlined
          required
          :placeholder="$t('username')"
          hide-details="auto"
          data-test="username-input"
          @update:model-value="onInput"
        />
        <VTextField
          v-model="credential.password"
          :rules="rules.password"
          :error-messages="login_err_msg"
          :type="isPwdVisible ? 'text' : 'password'"
          class="mt-6 v-text-field--message-up"
          name="password"
          autocomplete="current-password"
          single-line
          dense
          :height="36"
          outlined
          required
          :placeholder="$t('password')"
          hide-details="auto"
          data-test="pwd-input"
          @update:model-value="onInput"
        >
          <template #append-inner>
            <VIcon
              size="20"
              :icon="isPwdVisible ? '$mdiEyeOff' : '$mdiEye'"
              data-test="toggle-pwd-visibility-btn"
              @click="isPwdVisible = !isPwdVisible"
            />
          </template>
        </VTextField>
        <VCheckboxBtn
          v-model="rememberMe"
          class="ml-n1 mt-3 mb-4 remember-me-checkbox"
          :label="$t('rememberMe')"
          density="compact"
        >
          <template #label>
            <span class="text-navigation remember-me">{{ $t('rememberMe') }}</span>
          </template>
        </VCheckboxBtn>
      </VForm>
    </VCardText>
    <VCardActions style="padding-bottom: 60px" class="pt-0">
      <div class="mx-auto text-center">
        <VProgressCircular
          v-if="isLoading"
          :size="36"
          :width="5"
          color="primary"
          indeterminate
          class="mb-3"
        />
        <VBtn
          v-else
          :min-width="210"
          class="mx-auto"
          block
          rounded
          color="primary"
          variant="flat"
          data-test="login-btn"
          @click="handleLogin"
        >
          <span class="font-weight-bold text-capitalize">{{ $t('signIn') }}</span>
        </VBtn>
      </div>
    </VCardActions>
  </VCard>
</template>

<style lang="scss" scoped>
:deep(.v-text-field--message-up) {
  .v-input__details {
    position: absolute;
    transform: translateY(-120%);
  }
}
</style>
