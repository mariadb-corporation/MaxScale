<template>
    <v-combobox
        v-bind="{ ...$attrs }"
        outlined
        class="vuetify-input--override mariadb-select-input error--text__bottom error--text__bottom--no-margin"
        :menu-props="{
            contentClass: 'mariadb-select-v-menu',
            bottom: true,
            offsetY: true,
        }"
        dense
        hide-details="auto"
        v-on="$listeners"
    >
        <template v-slot:item="{ item, on, attrs }">
            <div
                class="v-list-item__title d-flex align-center flex-row flex-grow-1"
                v-bind="attrs"
                v-on="on"
            >
                {{ item }}
                {{ item === defItem ? `(${$mxs_t('default')})` : '' }}
            </div>
        </template>
    </v-combobox>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-12
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'charset-collate-select',
    inheritAttrs: false,
    props: {
        defItem: { type: String, required: true },
    },
}
</script>
