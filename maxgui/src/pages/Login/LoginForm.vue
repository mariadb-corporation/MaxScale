<template>
    <v-card class="v-card-custom" style="max-width: 463px; z-index: 2;border-radius: 10px;">
        <v-card-text style="padding:60px 80px 0px" align-center>
            <div class="">
                <h1 align="left" class="pb-4 color text-deep-ocean">
                    {{ $t('welcome') }}
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
                        :error-messages="errorMessage"
                        class="std mt-5"
                        name="username"
                        autocomplete="username"
                        autofocus
                        dense
                        single-line
                        outlined
                        required
                        :placeholder="$t('username')"
                        @input="onInput"
                    />
                    <v-text-field
                        id="password"
                        v-model="credential.password"
                        :rules="rules.password"
                        :error-messages="showEmptyMessage ? ' ' : errorMessage"
                        :type="isPwdVisible ? 'text' : 'password'"
                        class="std std-password mt-5"
                        name="password"
                        autocomplete="current-password"
                        single-line
                        dense
                        outlined
                        required
                        :placeholder="$t('password')"
                        @input="onInput"
                    >
                        <v-icon slot="append" size="20" @click="isPwdVisible = !isPwdVisible">
                            {{ isPwdVisible ? 'visibility_off' : 'visibility' }}
                        </v-icon>
                    </v-text-field>
                    <v-checkbox
                        v-model="rememberMe"
                        class="small mt-2 mb-4"
                        :label="$t('rememberMe')"
                        color="primary"
                        hide-details
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
                    <span class="font-weight-bold text-capitalize">{{ $t('signIn') }}</span>
                </v-btn>
                <!--  <a href style="font-size:0.75rem;text-decoration: none;" class="d-block mx-auto "
                    >{{ $t('forgotPassword') }}
                </a> -->
            </div>
        </v-card-actions>
    </v-card>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-06-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapMutations } from 'vuex'
import { refreshAxiosToken } from 'plugins/axios'

export default {
    name: 'login-form',

    data() {
        return {
            isValid: false,
            isLoading: false,

            isPwdVisible: false,
            rememberMe: false,
            credential: {
                username: '',
                password: '',
            },
            errorMessage: '',
            showEmptyMessage: false, // errors receives from api
            rules: {
                username: [
                    val => !!val || this.$t('errors.requiredInput', { inputName: 'Username' }),
                ],
                password: [
                    val => !!val || this.$t('errors.requiredInput', { inputName: 'Password' }),
                ],
            },
        }
    },
    computed: {
        logger: function() {
            return this.$logger('Login')
        },
    },

    methods: {
        ...mapMutations({ SET_LOGGED_IN_USER: 'user/SET_LOGGED_IN_USER' }),

        onInput() {
            if (this.showEmptyMessage) {
                this.showEmptyMessage = false
                this.errorMessage = ''
            }
        },

        async handleSubmit() {
            this.isLoading = true
            try {
                /*  use login axios instance, instead of showing global interceptor, show error in catch
                    max-age param will be 8 hours if rememberMe is true, otherwise, along as user close the browser
                    it will be expired
                */
                refreshAxiosToken()
                let url = '/auth?persist=yes'
                await this.$loginAxios.get(`${url}${this.rememberMe ? '&max-age=28800' : ''}`, {
                    auth: this.credential,
                })

                // for now, using username as name
                let userObj = {
                    name: this.credential.username,
                    rememberMe: this.rememberMe,
                    isLoggedIn: this.$help.getCookie('token_body') ? true : false,
                }
                await this.SET_LOGGED_IN_USER(userObj)
                localStorage.setItem('user', JSON.stringify(userObj))

                await this.$router.push(this.$route.query.redirect || '/dashboard/servers')
            } catch (error) {
                this.showEmptyMessage = true
                if (error.response) {
                    this.errorMessage =
                        error.response.status === 401
                            ? this.$t('errors.wrongCredentials')
                            : error.response.statusText
                } else {
                    this.logger.error(error)
                    this.errorMessage = error.toString()
                }
            }
            this.isLoading = false
        },
    },
}
</script>
