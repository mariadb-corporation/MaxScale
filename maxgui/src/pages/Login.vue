<template>
    <v-container v-resize="onResize" class="pa-0 ma-0 container--fluid login-wrapper fill-height ">
        <v-row class="pa-0 ma-0 ">
            <v-col class="pa-0 ma-0 " cols="12" align="center">
                <div class="logo">
                    <img src="@/assets/logo.svg" alt="MariaDB Logo" />
                    <span class="product-name font-weight-medium ml-2 white--text">
                        {{ config.productName }}
                    </span>
                </div>
                <v-card
                    class="v-card-custom"
                    style="max-width: 463px; z-index: 2;border-radius: 10px;"
                >
                    <v-card-text style="padding:60px 80px 0px" align-center>
                        <div class="">
                            <h1 align="left" class="pb-4" style="color: #003545;">
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
                                    :icon="isPwdVisible ? 'visibility_off' : 'visibility'"
                                    :rules="rules.password"
                                    :error-messages="showEmptyMessage ? ' ' : errorMessage"
                                    :type="isPwdVisible ? 'text' : 'password'"
                                    class="std mt-5"
                                    name="password"
                                    autocomplete="current-password"
                                    single-line
                                    dense
                                    outlined
                                    required
                                    :placeholder="$t('password')"
                                    @input="onInput"
                                    @click:append="isPwdVisible = !isPwdVisible"
                                />

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
                                <span class="font-weight-bold text-capitalize">{{
                                    $t('signIn')
                                }}</span>
                            </v-btn>
                            <!-- <a
                                href
                                style="font-size:0.75rem;text-decoration: none;"
                                class="d-block mx-auto "
                                >{{ $t('forgotPassword') }}
                            </a> -->
                        </div>
                    </v-card-actions>
                </v-card>
            </v-col>
            <canvas ref="canvas"></canvas>
        </v-row>
    </v-container>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState, mapMutations } from 'vuex'

export default {
    name: 'login',
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
            circles: [],
            scratch: document.createElement('canvas'),
            ctx: null,
            hasFocus: true,
        }
    },
    computed: {
        ...mapState(['config']),
    },

    mounted() {
        window.onfocus = () => {
            this.hasFocus = true
        }

        window.onblur = () => {
            this.hasFocus = false
        }

        if (window.requestAnimationFrame && this.$refs.canvas) {
            this.ctx = this.$refs.canvas.getContext('2d')

            this.createCircle() // Same UX with monitoring application, copy and paste :D

            window.requestAnimationFrame(this.draw)
        }
    },
    methods: {
        ...mapMutations({ setUser: 'user/setUser' }),

        onInput() {
            if (this.showEmptyMessage) {
                this.showEmptyMessage = false
                this.errorMessage = ''
            }
        },

        async handleSubmit() {
            this.isLoading = true
            let self = this
            try {
                /*   const login = axios.create() */
                /*  use login axios instance, instead of showing global interceptor, show error in catch
                    max-age param will be 8 hours if rememberMe is true, otherwise, along as user close the browser
                    it will be expired
                */
                let url = '/auth?persist=yes'
                await this.loginAxios.get(`${url}${self.rememberMe ? '&max-age=28800' : ''}`, {
                    auth: self.credential,
                })

                // for now, using username as name
                let userObj = {
                    name: self.credential.username,
                    rememberMe: self.rememberMe,
                    isLoggedIn: self.$help.getCookie('token_body') ? true : false,
                }
                await self.setUser(userObj)
                localStorage.setItem('user', JSON.stringify(userObj))

                await self.$router.push(self.$route.query.redirect || '/dashboard/servers')
            } catch (error) {
                this.showEmptyMessage = true
                this.errorMessage =
                    error.response.status === 401
                        ? this.$t('errors.wrongCredentials')
                        : error.response.statusText
            }
            this.isLoading = false
        },
        onResize() {
            let width =
                window.innerWidth && document.documentElement.clientWidth
                    ? Math.min(window.innerWidth, document.documentElement.clientWidth)
                    : window.innerWidth ||
                      document.documentElement.clientWidth ||
                      document.getElementsByTagName('body')[0].clientWidth
            let height =
                window.innerHeight && document.documentElement.clientHeight
                    ? Math.min(window.innerHeight, document.documentElement.clientHeight)
                    : window.innerHeight ||
                      document.documentElement.clientHeight ||
                      document.getElementsByTagName('body')[0].clientHeight

            this.scratch.width = this.$refs.canvas.width = width
            this.scratch.height = this.$refs.canvas.height = height
        },
        drawCircle(circle) {
            this.ctx.strokeStyle = circle.color
            this.ctx.lineWidth = 3
            this.ctx.globalAlpha = Math.max(0, circle.opacity)

            this.ctx.beginPath()
            this.ctx.arc(circle.x, circle.y, circle.radius, 0, 2 * Math.PI, true)
            this.ctx.stroke()
        },
        draw() {
            if (!this.$refs.canvas) return

            this.ctx.clearRect(0, 0, this.scratch.width, this.scratch.height)
            this.ctx.save()

            for (let i = this.circles.length - 1; i >= 0; --i) {
                this.drawCircle(this.circles[i])

                this.circles[i].radius += 3
                this.circles[i].opacity -= 0.0025
                if (this.circles[i].radius > this.$refs.canvas.width) this.circles.splice(i, 1)
            }

            this.ctx.restore()

            window.requestAnimationFrame(this.draw)
        },
        createCircle() {
            setTimeout(() => {
                if (this.hasFocus) {
                    this.circles.unshift({
                        x: window.innerWidth - 50,
                        y: window.innerHeight - 80,
                        radius: 1,
                        opacity: 0.9,
                        color: 'white',
                    })
                }

                this.createCircle()
            }, this.$help.range(2.5, 5) * 1000)
        },
    },
}
</script>

<style lang="scss" scoped>
.login-wrapper {
    width: 100%;
    height: 100%;
    background: radial-gradient(1100px at 100% 89%, $accent 0%, $deep-ocean 100%);
}
.logo {
    margin-bottom: 5px;

    & img {
        width: 265px;
        vertical-align: middle;
    }

    & span {
        display: inline-block;
        margin-top: 2px;
        font-size: 31px;
        vertical-align: middle;

        :after,
        :before {
            box-sizing: initial;
        }
    }
}

.login-btn {
    max-width: 50%;
    height: 36px;
    border-radius: 17px;
}
canvas {
    position: absolute;
    top: 0;
    left: 0;
}
</style>
