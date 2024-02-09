<template>
    <v-card class="v-card--mariadb" style="max-width: 463px; z-index: 2;border-radius: 10px;">
        <v-card-text style="padding:60px 80px 0px" align-center>
            <div class="">
                <h1 align="left" class="pb-4 mxs-color-helper text-deep-ocean">
                    {{ $mxs_t('welcome') }}
                </h1>
                <v-form
                    ref="form"
                    v-model="isValid"
                    class="pt-4"
                    @keyup.native.enter="isValid && handleSubmit()"
                >
                    <v-text-field
                        id="username"
                        v-model="credential.username"
                        :rules="rules.username"
                        :error-messages="login_err_msg"
                        class="vuetify-input--override mt-5"
                        name="username"
                        autocomplete="username"
                        autofocus
                        dense
                        :height="36"
                        single-line
                        outlined
                        required
                        :placeholder="$mxs_t('username')"
                        @input="onInput"
                    />
                    <v-text-field
                        id="password"
                        v-model="credential.password"
                        :rules="rules.password"
                        :error-messages="login_err_msg"
                        :type="isPwdVisible ? 'text' : 'password'"
                        class="vuetify-input--override vuetify-input--override-password mt-5"
                        name="password"
                        autocomplete="current-password"
                        single-line
                        dense
                        :height="36"
                        outlined
                        required
                        :placeholder="$mxs_t('password')"
                        @input="onInput"
                    >
                        <v-icon slot="append" size="20" @click="isPwdVisible = !isPwdVisible">
                            {{ isPwdVisible ? 'mdi-eye-off' : 'mdi-eye' }}
                        </v-icon>
                    </v-text-field>
                    <v-checkbox
                        v-model="rememberMe"
                        class="v-checkbox--mariadb mt-2 mb-4"
                        :label="$mxs_t('rememberMe')"
                        color="primary"
                        hide-details
                        dense
                    />
                </v-form>
            </div>
        </v-card-text>
        <v-card-actions style="padding-bottom:60px" class="pt-0 ">
            <div class="mx-auto text-center" style="width: 50%;">
                <v-progress-circular
                    v-if="isLoading"
                    :size="36"
                    :width="5"
                    color="primary"
                    indeterminate
                    class="mb-3"
                />
                <v-btn
                    v-else
                    :disabled="!isValid"
                    class="mx-auto login-btn mb-3"
                    block
                    depressed
                    color="primary"
                    small
                    @click="handleSubmit"
                >
                    <span class="font-weight-bold text-capitalize">{{ $mxs_t('signIn') }}</span>
                </v-btn>
            </div>
        </v-card-actions>
    </v-card>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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

import { mapActions, mapMutations, mapState } from 'vuex'
export default {
    name: 'login-form',

    data() {
        return {
            isValid: false,
            isLoading: false,

            isPwdVisible: false,
            rememberMe: true,
            credential: {
                username: '',
                password: '',
            },
            rules: {
                username: [
                    val => !!val || this.$mxs_t('errors.requiredInput', { inputName: 'Username' }),
                ],
                password: [
                    val => !!val || this.$mxs_t('errors.requiredInput', { inputName: 'Password' }),
                ],
            },
        }
    },
    computed: {
        ...mapState({
            login_err_msg: state => state.user.login_err_msg,
        }),
    },

    methods: {
        ...mapMutations({
            SET_LOGGED_IN_USER: 'user/SET_LOGGED_IN_USER',
            SET_LOGIN_ERR_MSG: 'user/SET_LOGIN_ERR_MSG',
        }),
        ...mapActions({ login: 'user/login' }),
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
