<template>
    <div class="mxs-query-editor">
        <query-editor ref="queryEditor" v-on="$listeners">
            <slot v-for="(_, slot) in $slots" :slot="slot" :name="slot" />
        </query-editor>
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
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
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
import QueryEditor from '@queryEditorSrc/QueryEditor.vue'
import { mapState } from 'vuex'

export default /*#__PURE__*/ {
    name: 'mxs-query-editor',
    components: { QueryEditor, LoadingTransparentOverlay, Snackbars },
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
