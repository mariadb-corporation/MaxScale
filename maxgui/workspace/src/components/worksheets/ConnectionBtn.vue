<template>
    <mxs-tooltip-btn
        :tooltipProps="{ disabled: !connectedServerName }"
        x-small
        text
        color="primary"
        v-bind="{ ...$attrs }"
        v-on="$listeners"
    >
        <template v-slot:btn-content>
            <v-icon size="14" color="primary" class="mr-1">
                mdi-server
            </v-icon>
            {{ btnTxt }}
        </template>
        {{ $mxs_t('changeConn') }}
    </mxs-tooltip-btn>
</template>
<script>
/*
 * Copyright (c) 2023 MariaDB plc
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

export default {
    name: 'connection-btn',
    inheritAttrs: false,
    props: {
        activeConn: { type: Object, required: true },
    },
    computed: {
        connectedServerName() {
            return this.$typy(this.activeConn, 'meta.name').safeString
        },
        btnTxt() {
            return this.connectedServerName || this.$mxs_t('connect')
        },
    },
}
</script>
