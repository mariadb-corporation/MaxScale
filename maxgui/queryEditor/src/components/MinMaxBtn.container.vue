<template>
    <v-tooltip
        top
        transition="slide-y-transition"
        content-class="shadow-drop mxs-color-helper text-navigation py-1 px-4"
    >
        <template v-slot:activator="{ on }">
            <v-btn icon small class="min-max-btn" v-on="on" @click="SET_FULLSCREEN(!is_fullscreen)">
                <v-icon size="22" color="accent-dark">
                    mdi-fullscreen{{ is_fullscreen ? '-exit' : '' }}
                </v-icon>
            </v-btn>
        </template>
        <span>{{ is_fullscreen ? $mxs_t('minimize') : $mxs_t('maximize') }}</span>
    </v-tooltip>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-08-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapMutations, mapState } from 'vuex'

export default {
    name: 'min-max-btn-ctr',
    computed: { ...mapState({ is_fullscreen: state => state.wke.is_fullscreen }) },
    methods: { ...mapMutations({ SET_FULLSCREEN: 'wke/SET_FULLSCREEN' }) },
}
</script>
