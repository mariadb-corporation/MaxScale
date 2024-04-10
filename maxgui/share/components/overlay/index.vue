<template>
    <v-fade-transition>
        <loading-transparent-overlay
            v-if="transparentLoading"
            class="v-overlay--custom transparent-loading"
        />
        <loading-overlay
            v-else-if="isLoadingOverlay"
            :showWelcomeTxt="isLogging"
            :msg="msg"
            class="v-overlay--custom"
        />
    </v-fade-transition>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { OVERLAY_LOGOUT, OVERLAY_LOGGING, OVERLAY_TRANSPARENT_LOADING } from '@share/overlayTypes'
import LoadingTransparentOverlay from './LoadingTransparentOverlay'
import LoadingOverlay from './LoadingOverlay'
import { mapState } from 'vuex'

export default {
    name: 'overlay',
    components: {
        'loading-overlay': LoadingOverlay,
        'loading-transparent-overlay': LoadingTransparentOverlay,
    },
    computed: {
        ...mapState({
            overlay_type: state => state.mxsApp.overlay_type,
            is_session_alive: state => state.mxsApp.is_session_alive,
        }),
        transparentLoading() {
            return this.overlay_type === OVERLAY_TRANSPARENT_LOADING
        },
        isLoadingOverlay() {
            return this.overlay_type === OVERLAY_LOGGING || this.overlay_type === OVERLAY_LOGOUT
        },
        isLogging() {
            return this.overlay_type === OVERLAY_LOGGING
        },
        msg() {
            switch (this.overlay_type) {
                case OVERLAY_LOGGING:
                    return this.$mxs_t('initializing')
                case OVERLAY_LOGOUT: {
                    let txt = this.$mxs_t('loggingOut')
                    if (!this.is_session_alive) txt = `${this.$mxs_t('sessionExpired')}. ${txt}`
                    return txt
                }
                default:
                    return ''
            }
        },
    },
}
</script>
<style lang="scss">
.v-overlay--custom {
    .v-overlay__scrim,
    .v-overlay__content {
        width: 100vw;
        height: 100vh;
    }
}
.v-overlay--custom.transparent-loading {
    .v-overlay__content {
        display: flex;
        justify-content: center;
        align-items: center;
    }
}
</style>
