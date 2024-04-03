<template>
    <mxs-tooltip-btn
        btnClass="toolbar-square-btn"
        text
        color="primary"
        v-bind="{ ...$attrs }"
        v-on="$listeners"
    >
        <template v-slot:btn-content>
            <v-icon size="16">$vuetify.icons.mxs_running</v-icon>
        </template>
        {{ $mxs_t('apply') }}
    </mxs-tooltip-btn>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
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

export default {
    name: 'apply-btn',
    inheritAttrs: false,
}
</script>
