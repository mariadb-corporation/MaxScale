<template>
    <mxs-dlg
        :title="$mxs_t('confirmations.leavePage')"
        saveText="confirm"
        minBodyWidth="624px"
        :onSave="onConfirm"
        v-bind="{ ...$attrs }"
        v-on="$listeners"
    >
        <template v-slot:form-body>
            <p data-test="disconnect-info">{{ $mxs_t('info.disconnectAll') }}</p>
            <v-checkbox
                v-model="confirmDelAll"
                class="v-checkbox--mariadb"
                :label="$mxs_t('disconnectAll')"
                color="primary"
                dense
                hide-details
            />
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
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
 * Events
 * on-confirm: (confirmDelAll) (boolean):
 */
export default {
    name: 'confirm-leave-dlg',
    inheritAttrs: false,
    data() {
        return {
            confirmDelAll: true,
        }
    },
    methods: {
        onConfirm() {
            this.$emit('on-confirm', this.confirmDelAll)
        },
    },
}
</script>
