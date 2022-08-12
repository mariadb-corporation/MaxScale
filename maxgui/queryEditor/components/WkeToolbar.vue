<template>
    <div class="wke-toolbar d-flex align-center flex-grow-1 color border-bottom-table-border px-2">
        <div ref="leftBtns" class="d-flex align-center left-buttons fill-height">
            <v-btn
                :disabled="isAddWkeDisabled"
                small
                class="float-left add-wke-btn"
                icon
                @click="asyncEmit('on-add-wke')"
            >
                <v-icon size="18" color="deep-ocean">mdi-plus</v-icon>
            </v-btn>
        </div>
        <v-spacer />
        <div ref="rightBtns" class="d-flex align-center right-buttons fill-height">
            <conn-man-ctr class="mx-2" />
            <v-tooltip
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
            >
                <template v-slot:activator="{ on }">
                    <v-btn
                        icon
                        small
                        class="query-setting-btn"
                        v-on="on"
                        @click="queryConfigDialog = !queryConfigDialog"
                    >
                        <v-icon size="16" color="accent-dark">
                            $vuetify.icons.settings
                        </v-icon>
                    </v-btn>
                </template>
                <span class="text-capitalize"> {{ $tc('settings', 2) }}</span>
            </v-tooltip>
            <v-tooltip
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
            >
                <template v-slot:activator="{ on }">
                    <v-btn
                        icon
                        small
                        class="min-max-btn"
                        v-on="on"
                        @click="$emit('on-fullscreen-click')"
                    >
                        <v-icon size="22" color="accent-dark">
                            mdi-fullscreen{{ isFullscreen ? '-exit' : '' }}
                        </v-icon>
                    </v-btn>
                </template>
                <span>{{ isFullscreen ? $t('minimize') : $t('maximize') }}</span>
            </v-tooltip>
        </div>

        <query-cnf-dlg-ctr v-model="queryConfigDialog" />
    </div>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-08-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
 * Emits
 * $emit('get-total-btn-width', v:number)
 * asyncEmit('on-add-wke')
 * $emit('on-fullscreen-click')
 */
import QueryCnfDlg from './QueryCnfDlg.container.vue'
import ConnMan from './ConnMan.container.vue'
import asyncEmit from '@share/mixins/asyncEmit'

export default {
    name: 'wke-toolbar',
    components: {
        'query-cnf-dlg-ctr': QueryCnfDlg,
        'conn-man-ctr': ConnMan,
    },
    mixins: [asyncEmit],
    props: {
        isFullscreen: { type: Boolean, required: true },
        isAddWkeDisabled: { type: Boolean, required: true },
    },
    data() {
        return {
            queryConfigDialog: false,
        }
    },
    mounted() {
        this.$nextTick(() =>
            this.$emit(
                'get-total-btn-width',
                this.$refs.rightBtns.clientWidth + this.$refs.leftBtns.clientWidth
            )
        )
    },
}
</script>
