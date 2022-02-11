<template>
    <collapse
        :toggleOnClick="() => (showInputs = !showInputs)"
        :isContentVisible="showInputs"
        wrapperClass="tbl-opts px-1 pt-2"
        :title="$t('alterTbl')"
    >
        <template v-slot:arrow-toggle="{ toggleOnClick, isContentVisible }">
            <v-btn icon small class="arrow-toggle" @click="toggleOnClick">
                <v-icon
                    :class="[isContentVisible ? 'arrow-down' : 'arrow-right']"
                    size="28"
                    color="deep-ocean"
                >
                    $expand
                </v-icon>
            </v-btn>
        </template>
        <template v-slot:content>
            <v-container fluid class="py-0 px-1 pb-3">
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
                            :height="28"
                            class="std error--text__bottom"
                            hide-details="auto"
                            dense
                            outlined
                        />
                    </v-col>
                    <v-col cols="12" md="6" class="py-0 px-1">
                        <label class="field__label color text-small-text">
                            {{ $t('comment') }}
                        </label>
                        <v-text-field
                            v-model="tableOptsData.table_comment"
                            class="std error--text__bottom error--text__bottom--no-margin"
                            single-line
                            outlined
                            dense
                            :height="28"
                            hide-details="auto"
                        />
                    </v-col>
                </v-row>
                <v-row class="ma-0">
                    <v-col cols="6" md="4" class="py-0 px-1">
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
                            :height="28"
                            hide-details="auto"
                        />
                    </v-col>
                    <v-col cols="6" md="4" class="py-0 px-1">
                        <label class="field__label color text-small-text label-required">
                            {{ $t('charset') }}
                        </label>
                        <charset-input
                            v-model="tableOptsData.table_charset"
                            :defCharset="defDbCharset"
                            :height="28"
                            @on-input="onInputCharset"
                        />
                    </v-col>
                    <v-col cols="6" md="4" class="py-0 px-1">
                        <label class="field__label color text-small-text label-required">
                            {{ $t('collation') }}
                        </label>
                        <collation-input
                            v-model="tableOptsData.table_collation"
                            :defCollation="defCollation"
                            :height="28"
                            :charset="tableOptsData.table_charset"
                        />
                    </v-col>
                </v-row>
            </v-container>
        </template>
    </collapse>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
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
        initialData: { type: Object, required: true },
    },
    data() {
        return {
            rules: {
                table_name: [
                    val => !!val || this.$t('errors.requiredInput', { inputName: this.$t('name') }),
                ],
            },
            showInputs: true,
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
    watch: {
        initialData: {
            deep: true,
            handler(v, oV) {
                // show the content when alter "new" table
                if (!this.$help.lodash.isEqual(v, oV)) this.showInputs = true
            },
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

<style lang="scss">
.tbl-opts {
    .collapse-title {
        font-size: 0.75rem !important;
    }
}
</style>
