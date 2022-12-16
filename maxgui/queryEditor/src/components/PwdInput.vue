<template>
    <div>
        <label class="field__label mxs-color-helper text-small-text label-required">
            {{ $mxs_t('password') }}
        </label>
        <v-text-field
            v-bind="{ ...$attrs }"
            :rules="rule"
            :type="isPwdVisible ? 'text' : 'password'"
            class="vuetify-input--override error--text__bottom password"
            autocomplete="new-password"
            dense
            :height="36"
            hide-details="auto"
            outlined
            required
            v-on="$listeners"
        >
            <v-icon slot="append" size="20" @click="isPwdVisible = !isPwdVisible">
                {{ isPwdVisible ? 'mdi-eye-off' : 'mdi-eye' }}
            </v-icon>
        </v-text-field>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    name: 'pwd-input',
    inheritAttrs: false,
    data() {
        return {
            isPwdVisible: false,
            rule: [
                val =>
                    !!val ||
                    this.$mxs_t('errors.requiredInput', { inputName: this.$mxs_t('password') }),
            ],
        }
    },
}
</script>
