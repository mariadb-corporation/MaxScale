<template>
    <mxs-tooltip-btn
        btnClass="toolbar-square-btn"
        text
        color="primary"
        v-bind="{ ...$attrs }"
        v-on="$listeners"
    >
        <template v-slot:btn-content>
            <v-icon size="16">$vuetify.icons.mxs_reload</v-icon>
        </template>
        {{ $mxs_t('revertChanges') }}
    </mxs-tooltip-btn>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
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

export default {
    name: 'revert-btn',
    inheritAttrs: false,
}
</script>
