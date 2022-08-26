<template>
    <div ref="wrapper" class="d-flex align-center right-buttons fill-height">
        <conn-man-ctr class="mx-2" />
        <v-tooltip
            top
            transition="slide-y-transition"
            content-class="shadow-drop mxs-color-helper text-navigation py-1 px-4"
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
                        $vuetify.icons.mxs_settings
                    </v-icon>
                </v-btn>
            </template>
            <span class="text-capitalize"> {{ $mxs_tc('settings', 2) }}</span>
        </v-tooltip>
        <v-tooltip
            top
            transition="slide-y-transition"
            content-class="shadow-drop mxs-color-helper text-navigation py-1 px-4"
        >
            <template v-slot:activator="{ on }">
                <v-btn
                    icon
                    small
                    class="min-max-btn"
                    v-on="on"
                    @click="SET_FULLSCREEN(!is_fullscreen)"
                >
                    <v-icon size="22" color="accent-dark">
                        mdi-fullscreen{{ is_fullscreen ? '-exit' : '' }}
                    </v-icon>
                </v-btn>
            </template>
            <span>{{ is_fullscreen ? $mxs_t('minimize') : $mxs_t('maximize') }}</span>
        </v-tooltip>
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

import { mapMutations, mapState } from 'vuex'
import ConnMan from './ConnMan.container.vue'
import QueryCnfDlg from './QueryCnfDlg.container.vue'
export default {
    name: 'wke-toolbar-right-btns-ctr',
    components: { 'conn-man-ctr': ConnMan, 'query-cnf-dlg-ctr': QueryCnfDlg },
    data() {
        return { queryConfigDialog: false }
    },
    computed: {
        ...mapState({ is_fullscreen: state => state.wke.is_fullscreen }),
    },
    mounted() {
        this.$nextTick(() => this.$emit('get-total-width', this.$refs.wrapper.clientWidth))
    },
    methods: {
        ...mapMutations({ SET_FULLSCREEN: 'wke/SET_FULLSCREEN' }),
    },
}
</script>
