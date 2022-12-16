<template>
    <div>
        <label
            class="field__label mxs-color-helper text-small-text"
            :class="{ 'label-required': $attrs.required }"
        >
            {{ $mxs_t('database') }}
        </label>
        <v-text-field
            v-bind="{ ...$attrs }"
            class="vuetify-input--override error--text__bottom"
            name="db"
            dense
            :height="36"
            hide-details="auto"
            outlined
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
    name: 'db-input',
    inheritAttrs: false,
}
</script>
