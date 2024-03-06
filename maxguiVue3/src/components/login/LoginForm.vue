<script>
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

import { mapActions, mapMutations, mapState } from 'vuex'
export default {
  name: 'LoginForm',
  data() {
    return {
      formValidity: null,
      isLoading: false,
      isPwdVisible: false,
      rememberMe: true,
      credential: {
        username: '',
        password: '',
      },
      rules: {
        username: [(val) => !!val || this.$t('errors.requiredInput', { inputName: 'Username' })],
        password: [(val) => !!val || this.$t('errors.requiredInput', { inputName: 'Password' })],
      },
    }
  },
  computed: {
    ...mapState({
      login_err_msg: (state) => state.users.login_err_msg,
    }),
  },
  methods: {
    ...mapMutations({
      SET_LOGGED_IN_USER: 'users/SET_LOGGED_IN_USER',
      SET_LOGIN_ERR_MSG: 'users/SET_LOGIN_ERR_MSG',
    }),
    ...mapActions({ login: 'users/login' }),
    onInput() {
      if (this.login_err_msg) this.SET_LOGIN_ERR_MSG('')
    },
    async handleSubmit() {
      this.isLoading = true
      await this.login({
        rememberMe: this.rememberMe,
        auth: this.credential,
      })
      this.isLoading = false
    },
  },
}
</script>
<template>
  <VCard
    :min-width="463"
    style="z-index: 2; border-radius: 10px"
    elevation="5"
    class="px-10 pb-3"
    tabindex="0"
    @keyup.enter="formValidity && handleSubmit()"
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
          @update:model-value="onInput"
        >
          <template #append-inner>
            <VIcon
              size="20"
              :icon="isPwdVisible ? '$mdiEyeOff' : '$mdiEye'"
              @click="isPwdVisible = !isPwdVisible"
            />
          </template>
        </VTextField>
        <VCheckbox
          v-model="rememberMe"
          class="ml-n1 mt-3 mb-4 v-checkbox--xs remember-me-checkbox"
          :label="$t('rememberMe')"
          color="primary"
          hide-details
          density="compact"
        />
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
          :min-height="36"
          class="mx-auto login-btn"
          block
          rounded
          color="primary"
          size="small"
          variant="flat"
          @click="handleSubmit"
        >
          <span class="font-weight-bold text-capitalize">{{ $t('signIn') }}</span>
        </VBtn>
      </div>
    </VCardActions>
  </VCard>
</template>

<style lang="scss">
.remember-me-checkbox {
  label {
    font-size: 0.75rem;
    color: colors.$navigation;
    font-weight: 400;
    opacity: 1;
  }
}

.v-text-field--message-up {
  .v-input {
    &__details {
      position: absolute;
      transform: translateY(-120%);
    }
  }
}
</style>
