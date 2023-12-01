<template>
    <v-tooltip top transition="slide-y-transition" v-bind="tooltipProps">
        <template v-slot:activator="{ on }">
            <v-btn :class="btnClass" v-bind="{ ...$attrs }" v-on="{ ...$listeners, ...on }">
                <slot name="btn-content" />
            </v-btn>
        </template>
        <span class="d-inline-block text-center text-capitalize">
            <slot />
        </span>
    </v-tooltip>
</template>
<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
    name: 'mxs-tooltip-btn',
    inheritAttrs: false,
    props: {
        btnClass: { type: [Array, Object, String], default: () => [] },
        tooltipProps: { type: Object, default: () => ({}) },
    },
}
</script>
