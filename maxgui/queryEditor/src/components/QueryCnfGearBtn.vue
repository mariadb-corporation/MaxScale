<template>
    <v-tooltip
        top
        transition="slide-y-transition"
        content-class="shadow-drop mxs-color-helper white text-navigation py-1 px-4"
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
            <query-cnf-dlg-ctr v-model="queryConfigDialog" />
        </template>
        <span class="text-capitalize"> {{ $mxs_tc('settings', 2) }}</span>
    </v-tooltip>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-09-06
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import QueryCnfDlgCtr from './QueryCnfDlgCtr.vue'
export default {
    name: 'query-cnf-gear-btn',
    components: { QueryCnfDlgCtr },
    data() {
        return { queryConfigDialog: false }
    },
}
</script>
