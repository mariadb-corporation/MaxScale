<template>
    <mxs-conf-dlg
        :title="$mxs_t('confirmations.leavePage')"
        saveText="confirm"
        minBodyWidth="624px"
        v-bind="{ ...$attrs }"
        v-on="$listeners"
    >
        <template v-slot:confirm-text>
            <p>{{ $mxs_t('info.disconnectAll') }}</p>
        </template>
        <template v-slot:body-append>
            <v-checkbox
                v-model="confirmDelAll"
                class="v-checkbox--custom-label"
                :label="$mxs_t('disconnectAll')"
                color="primary"
                dense
                hide-details
            />
        </template>
    </mxs-conf-dlg>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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
/*
 * Events
 * update:shouldDelAll?: (boolean)
 */
export default {
    name: 'confirm-leave-dlg',
    props: {
        shouldDelAll: {
            required: true,
            validator: prop => typeof prop === 'boolean' || prop === null,
            default: true,
        }, // sync prop
    },
    computed: {
        confirmDelAll: {
            get() {
                return this.shouldDelAll
            },
            set(value) {
                this.$emit('update:shouldDelAll', value)
            },
        },
    },
}
</script>
