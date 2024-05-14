<template>
    <mxs-label-field
        v-bind="{ ...$attrs }"
        :required="true"
        :type="isPwdVisible ? 'text' : 'password'"
        autocomplete="new-password"
        :label="$mxs_t('password')"
        v-on="$listeners"
    >
        <v-icon slot="append" size="20" @click="isPwdVisible = !isPwdVisible">
            {{ isPwdVisible ? 'mdi-eye-off' : 'mdi-eye' }}
        </v-icon>
    </mxs-label-field>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    name: 'mxs-pwd-input',
    inheritAttrs: false,
    data() {
        return {
            isPwdVisible: false,
        }
    },
}
</script>
