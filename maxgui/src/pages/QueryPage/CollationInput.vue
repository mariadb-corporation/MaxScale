<template>
    <v-combobox
        v-model="collation"
        :items="collations"
        outlined
        class="std mariadb-select-input error--text__bottom"
        :menu-props="{
            contentClass: 'mariadb-select-v-menu',
            bottom: true,
            offsetY: true,
        }"
        dense
        :height="height"
        hide-details="auto"
        :disabled="disabled"
        @input="$emit('on-input', $event)"
    >
        <template v-slot:item="{ item, on, attrs }">
            <div
                class="v-list-item__title d-flex align-center flex-row flex-grow-1"
                v-bind="attrs"
                v-on="on"
            >
                {{ item }}
                {{ item === defCollation ? `(${$t('defCollation')})` : '' }}
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
 * Change Date: 2025-11-19
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'
export default {
    name: 'collation-input',
    props: {
        value: { type: String },
        defCollation: { type: String, required: true },
        charset: { type: String },
        height: { type: Number, default: 36 },
        disabled: { type: Boolean, default: false },
    },
    computed: {
        ...mapState({
            charset_collation_map: state => state.query.charset_collation_map,
        }),
        collation: {
            get() {
                return this.value
            },
            set(v) {
                this.$emit('input', v)
            },
        },
        collations() {
            return this.$typy(this.charset_collation_map.get(this.charset), 'collations').safeArray
        },
    },
}
</script>

<style lang="sass" scoped></style>
