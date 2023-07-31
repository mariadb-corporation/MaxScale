<template>
    <mxs-tooltip-btn btnClass="min-max-btn" icon small @click="SET_FULLSCREEN(!is_fullscreen)">
        <template v-slot:btn-content>
            <v-icon size="22" color="accent-dark">
                mdi-fullscreen{{ is_fullscreen ? '-exit' : '' }}
            </v-icon>
        </template>
        {{ is_fullscreen ? $mxs_t('minimize') : $mxs_t('maximize') }}
    </mxs-tooltip-btn>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-07-24
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
