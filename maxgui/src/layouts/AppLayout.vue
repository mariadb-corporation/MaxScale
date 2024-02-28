<template>
    <div class="fill-height">
        <app-header />
        <navigation />
        <snackbars :msgObj="snackbar_message" />
        <v-main class="fill-height">
            <transition name="fade" mode="out-in">
                <router-view v-if="$route.meta.requiresAuth" />
            </transition>
        </v-main>
        <v-footer
            class="pl-2 d-flex mxs-color-helper white border-top-separator justify-center"
            padless
            app
            height="40"
            inset
        >
            <span class="footer-text mxs-color-helper text-code-color align-center text-truncate">
                {{ $mxs_t('mariadbCopyright') }}
                <span class="footer__separator" />
                <a
                    target="_blank"
                    rel="noopener noreferrer"
                    href="https://mariadb.com/privacy-policy/"
                    class="rsrc-link"
                >
                    {{ $mxs_t('privacyPolicy') }}
                </a>
                <span class="footer__separator" />
                <a
                    target="_blank"
                    rel="noopener noreferrer"
                    href="https://mariadb.com/product-terms-condition/"
                    class="rsrc-link"
                >
                    {{ $mxs_t('termsOfUse') }}
                </a>
            </span>
        </v-footer>
    </div>
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
import Navigation from './Navigation'
import AppHeader from './AppHeader'
import Snackbars from '@share/components/Snackbars'
import { mapState } from 'vuex'
export default {
    name: 'app-layout',
    components: {
        navigation: Navigation,
        'app-header': AppHeader,
        snackbars: Snackbars,
    },
    computed: {
        ...mapState({ snackbar_message: state => state.mxsApp.snackbar_message }),
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
