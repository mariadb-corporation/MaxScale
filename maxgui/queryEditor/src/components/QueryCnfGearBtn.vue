<template>
    <mxs-tooltip-btn
        btnClass="query-setting-btn"
        icon
        small
        @click="queryConfigDialog = !queryConfigDialog"
    >
        <template v-slot:btn-content>
            <v-icon size="16" color="accent-dark">$vuetify.icons.mxs_settings</v-icon>
            <query-cnf-dlg-ctr v-model="queryConfigDialog" />
        </template>
        {{ $mxs_tc('settings', 2) }}
    </mxs-tooltip-btn>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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

import QueryCnfDlgCtr from './QueryCnfDlgCtr.vue'
export default {
    name: 'query-cnf-gear-btn',
    components: { QueryCnfDlgCtr },
    data() {
        return { queryConfigDialog: false }
    },
}
</script>
