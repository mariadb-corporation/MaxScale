<template>
    <v-container fluid>
        <v-row class="ma-0">
            <v-col cols="12" md="6" class="py-0 px-1">
                <label class="field__label color text-small-text label-required">
                    {{ $t('name') }}
                </label>
                <v-text-field
                    id="table_name"
                    v-model="tableOptsData.table_name"
                    :rules="rules.table_name"
                    required
                    name="table_name"
                    :height="32"
                    class="std error--text__bottom"
                    hide-details="auto"
                    dense
                    outlined
                />
            </v-col>
            <v-col cols="12" md="6" class="py-0 px-1">
                <label class="field__label color text-small-text label-required">
                    {{ $t('engine') }}
                </label>
                <v-select
                    v-model="tableOptsData.table_engine"
                    :items="engines"
                    name="table_engine"
                    outlined
                    class="std mariadb-select-input error--text__bottom"
                    :menu-props="{
                        contentClass: 'mariadb-select-v-menu',
                        bottom: true,
                        offsetY: true,
                    }"
                    dense
                    :height="32"
                    hide-details="auto"
                />
            </v-col>
        </v-row>
        <v-row class="ma-0">
            <v-col cols="12" md="6" class="py-0 px-1">
                <label class="field__label color text-small-text label-required">
                    {{ $t('charset') }}
                </label>
                <charset-input
                    v-model="tableOptsData.table_charset"
                    :defCharset="defDbCharset"
                    @on-input="onInputCharset"
                />
            </v-col>
            <v-col cols="12" md="6" class="py-0 px-1">
                <label class="field__label color text-small-text label-required">
                    {{ $t('collation') }}
                </label>
                <collation-input
                    v-model="tableOptsData.table_collation"
                    :defCollation="defCollation"
                    :charset="tableOptsData.table_charset"
                />
            </v-col>
        </v-row>
        <v-row class="ma-0">
            <v-col cols="12" class="py-0 px-1">
                <label class="field__label color text-small-text">
                    {{ $t('comment') }}
                </label>
                <v-textarea
                    v-model="tableOptsData.table_comment"
                    class="std txt-area"
                    dense
                    auto-grow
                    outlined
                    rows="1"
                    hide-details="auto"
                />
            </v-col>
        </v-row>
    </v-container>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-09-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'
import CharsetInput from './CharsetInput.vue'
import CollationInput from './CollationInput.vue'
export default {
    name: 'alter-table-opts',
    components: {
        'charset-input': CharsetInput,
        'collation-input': CollationInput,
    },
    props: {
        value: { type: Object, required: true },
    },
    data() {
        return {
            rules: {
                table_name: [
                    val => !!val || this.$t('errors.requiredInput', { inputName: this.$t('name') }),
                ],
            },
        }
    },
    computed: {
        ...mapState({
            charset_collation_map: state => state.query.charset_collation_map,
            engines: state => state.query.engines,
            def_db_charset_map: state => state.query.def_db_charset_map,
        }),
        tableOptsData: {
            get() {
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
        defCollation() {
            return this.$typy(
                this.charset_collation_map.get(this.tableOptsData.table_charset),
                'defCollation'
            ).safeString
        },
        defDbCharset() {
            return this.$typy(this.def_db_charset_map.get(this.tableOptsData.dbName)).safeString
        },
    },
    methods: {
        onInputCharset() {
            // Use default collation of selected charset
            this.tableOptsData.table_collation = this.defCollation
        },
    },
}
</script>
