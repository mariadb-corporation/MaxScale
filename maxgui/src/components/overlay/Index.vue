<template>
    <v-fade-transition>
        <component :is="currentOverLay" :key="overlay" />
    </v-fade-transition>
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
import {
    OVERLAY_LOGOUT,
    OVERLAY_LOADING,
    OVERLAY_ERROR,
    OVERLAY_TRANSPARENT_LOADING,
} from 'store/overlayTypes'
import ErrorOverlay from './ErrorOverlay'
import LoadingTransparentOverlay from './LoadingTransparentOverlay'
import LoadingOverlay from './LoadingOverlay'
import LogoutOverlay from './LogoutOverlay'
import { mapGetters } from 'vuex'

export default {
    name: 'overlay',
    computed: {
        ...mapGetters(['overlay']),
        currentOverLay: function() {
            switch (this.overlay) {
                case OVERLAY_TRANSPARENT_LOADING:
                    return LoadingTransparentOverlay
                case OVERLAY_LOADING:
                    return LoadingOverlay
                case OVERLAY_LOGOUT:
                    return LogoutOverlay
                case OVERLAY_ERROR:
                    return ErrorOverlay
                default:
                    return false
            }
        },
    },
    watch: {
        overlay: function(newVal) {
            let html = document.getElementsByTagName('html')[0]

            if (newVal) {
                html.style.overflow = 'hidden'
            } else {
                html.style.overflow = 'auto'
            }
        },
    },
}
</script>
