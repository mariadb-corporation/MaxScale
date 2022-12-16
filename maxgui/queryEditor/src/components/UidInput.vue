<template>
    <div>
        <label class="field__label mxs-color-helper text-small-text label-required">
            {{ $mxs_t('username') }}
        </label>
        <v-text-field
            v-bind="{ ...$attrs }"
            :rules="rule"
            class="vuetify-input--override error--text__bottom user"
            autocomplete="new-username"
            dense
            :height="36"
            hide-details="auto"
            outlined
            required
            v-on="$listeners"
        />
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
    name: 'uid-input',
    inheritAttrs: false,
    data() {
        return {
            rule: [
                val =>
                    !!val ||
                    this.$mxs_t('errors.requiredInput', { inputName: this.$mxs_t('username') }),
            ],
        }
    },
}
</script>
