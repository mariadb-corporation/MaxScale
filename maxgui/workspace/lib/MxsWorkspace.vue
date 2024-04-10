<template>
    <div class="mxs-workspace">
        <workspace-ctr>
            <slot v-for="(_, slot) in $slots" :slot="slot" :name="slot" />
        </workspace-ctr>
        <v-fade-transition>
            <loading-transparent-overlay
                v-if="transparentLoading"
                class="v-overlay--custom transparent-loading"
            />
        </v-fade-transition>
        <snackbars :msgObj="snackbar_message" />
        <mxs-truncate-tooltip />
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
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import '@share/styles/color_helper.scss'
import '@share/styles/helpers.scss'
import '@share/styles/override.scss'
import '@share/styles/chartTooltip.scss'
import LoadingTransparentOverlay from '@share/components/overlay/LoadingTransparentOverlay'
import { OVERLAY_TRANSPARENT_LOADING } from '@share/overlayTypes'
import Snackbars from '@share/components/Snackbars'
import WorkspaceCtr from '@wsSrc/WorkspaceCtr.vue'
import { mapState } from 'vuex'

export default /*#__PURE__*/ {
    name: 'mxs-workspace',
    components: { WorkspaceCtr, LoadingTransparentOverlay, Snackbars },
    computed: {
        ...mapState({
            snackbar_message: state => state.mxsApp.snackbar_message,
            overlay_type: state => state.mxsApp.overlay_type,
        }),
        transparentLoading() {
            return this.overlay_type === OVERLAY_TRANSPARENT_LOADING
        },
    },
}
</script>
