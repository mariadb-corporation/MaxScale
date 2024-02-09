<template>
    <mxs-dlg v-bind="{ ...$attrs }" v-on="$listeners">
        <template v-slot:form-body>
            <slot name="confirm-text">
                <p
                    v-if="!$typy(item).isNull && type"
                    class="confirmations-text"
                    v-html="$mxs_t(`confirmations.${type}`, { targetId: item.id })"
                />
            </slot>
            <slot name="body-prepend"></slot>
            <small> {{ smallInfo }} </small>
            <slot name="body-append"></slot>
        </template>
        <!-- Pass on all named slots -->
        <template v-for="(_, slot) in $scopedSlots" v-slot:[slot]="props">
            <slot :name="slot" v-bind="props" />
        </template>
    </mxs-dlg>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-01-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'confirm-dlg',
    inheritAttrs: false,
    props: {
        type: { type: String, default: '' }, //check confirmations in en.json
        item: { type: Object, default: null }, // required when type is defined
        smallInfo: { type: String, default: '' },
    },
}
</script>
