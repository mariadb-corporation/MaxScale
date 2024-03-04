<template>
    <v-container v-resize="onResize" fluid class="login-wrapper fill-height">
        <v-row>
            <v-col cols="12" align="center">
                <div class="logo">
                    <img src="@share/assets/logo.svg" alt="MariaDB Logo" />
                    <span class="product-name font-weight-medium ml-2 white--text">
                        MaxScale
                    </span>
                </div>
                <login-form />
            </v-col>
            <intro-animation :bus="bus" />
        </v-row>
    </v-container>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Vue from 'vue'
import LoginForm from './LoginForm'
import IntroAnimation from './IntroAnimation'

export default {
    name: 'login',
    components: {
        LoginForm,
        IntroAnimation,
    },
    data() {
        return {
            bus: new Vue(),
        }
    },
    methods: {
        onResize() {
            this.bus.$emit('on-resize')
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
