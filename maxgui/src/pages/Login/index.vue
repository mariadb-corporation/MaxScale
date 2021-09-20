<template>
    <v-container v-resize="onResize" class="pa-0 ma-0 container--fluid login-wrapper fill-height ">
        <v-row class="pa-0 ma-0 ">
            <v-col class="pa-0 ma-0 " cols="12" align="center">
                <div class="logo">
                    <img src="@/assets/logo.svg" alt="MariaDB Logo" />
                    <span class="product-name font-weight-medium ml-2 white--text">
                        {{ app_config.productName }}
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
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Vue from 'vue'
import { mapState } from 'vuex'
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
    computed: {
        ...mapState(['app_config']),
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
