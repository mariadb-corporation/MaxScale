<template>
    <div class="fill-height">
        <app-header />
        <navigation />
        <snackbars />
        <v-content class="fill-height">
            <div class="fill-height py-6 px-10">
                <transition name="fade" mode="out-in">
                    <router-view v-if="$route.meta.requiresAuth" />
                </transition>
            </div>
        </v-content>
        <v-footer
            class="pl-2 d-flex color bg-background justify-center color border-top-reflection"
            padless
            app
            height="40"
            inset
        >
            <span class="footer-text color text-code-color align-center text-truncate">
                MariaDB Corporation <span class="footer__separator" />
                Copyright © 2020 MariaDB ab. All rights reserved.
                <span class="footer__separator" />
                <a target="_blank" rel="noopener" href="https://mariadb.com/privacy-policy/">
                    Privacy Policy</a
                >
                <span class="footer__separator" />
                <a
                    target="_blank"
                    rel="noopener"
                    href="https://mariadb.com/product-terms-condition/"
                >
                    Terms of Use
                </a>
            </span>
        </v-footer>
    </div>
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
import Navigation from './Navigation'
import AppHeader from './AppHeader'
import Snackbars from './Snackbars'

export default {
    name: 'app-layout',
    components: {
        navigation: Navigation,
        'app-header': AppHeader,
        snackbars: Snackbars,
    },
}
</script>
<style lang="scss">
.footer-text {
    font-size: 10px;
    a {
        text-decoration: none;
    }
}

/* Enter and leave animations can use different */
/* durations and timing functions.              */
.fade-enter-active,
.fade-leave-active {
    height: 100%;
    transition-duration: 0.2s;
    transition-property: opacity;
    transition-timing-function: ease;
}

.fade-enter,
.fade-leave-active {
    opacity: 0;
}
.footer__separator::after {
    content: '|';
    margin: 0 10px;
}
</style>
