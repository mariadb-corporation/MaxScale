<template>
    <div class="py-0 px-2 mb-4">
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
                <v-select
                    v-model="tableOptsData.table_charset"
                    :items="charsets"
                    name="table_charset"
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
                    @change="onChangeCharset"
                >
                    <template v-slot:item="{ item, on, attrs }">
                        <div
                            class="v-list-item__title d-flex align-center flex-row flex-grow-1"
                            v-bind="attrs"
                            v-on="on"
                        >
                            {{ item }}
                            {{ item === defDbCharset ? `(${$t('defCharset')})` : '' }}
                        </div>
                    </template>
                </v-select>
            </v-col>
            <v-col cols="12" md="6" class="py-0 px-1">
                <label class="field__label color text-small-text label-required">
                    {{ $t('collation') }}
                </label>
                <v-select
                    v-model="tableOptsData.table_collation"
                    :items="collations"
                    name="table_collation"
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
                </v-select>
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
    </div>
</template>

<script>
import { mapState } from 'vuex'
export default {
    name: 'alter-table-opts',
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
        charsets() {
            return [...this.charset_collation_map.keys()]
        },
        collations() {
            return this.$typy(
                this.charset_collation_map.get(this.tableOptsData.table_charset),
                'collations'
            ).safeArray
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
        onChangeCharset() {
            // Use default collation of selected charset
            this.tableOptsData.table_collation = this.defCollation
        },
    },
}
</script>
