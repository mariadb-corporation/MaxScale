<template>
    <v-form lazy-validation class="pa-4 pt-2">
        <v-row>
            <v-col cols="12" md="6" class="pa-1">
                <label class="field__label color text-small-text label-required">
                    {{ $t('name') }}
                </label>
                <v-text-field
                    id="table_name"
                    v-model="tableInfo.table_name"
                    :rules="rules.table_name"
                    required
                    validate-on-blur
                    name="table_name"
                    :height="32"
                    class="std error--text__bottom"
                    dense
                    outlined
                />
            </v-col>
            <v-col cols="12" md="6" class="pa-1">
                <label class="field__label color text-small-text label-required">
                    {{ $t('engine') }}
                </label>
                <v-select
                    v-model="tableInfo.table_engine"
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
                    :rules="rules.table_engine"
                    required
                    validate-on-blur
                />
            </v-col>
        </v-row>
        <v-row>
            <v-col cols="12" md="6" class="pa-1">
                <label class="field__label color text-small-text label-required">
                    {{ $t('charset') }}
                </label>
                <v-select
                    v-model="tableInfo.table_charset"
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
                    :rules="rules.table_charset"
                    required
                    validate-on-blur
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
            <v-col cols="12" md="6" class="pa-1">
                <label class="field__label color text-small-text label-required">
                    {{ $t('collation') }}
                </label>
                <v-select
                    v-model="tableInfo.table_collation"
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
                    :rules="rules.table_collation"
                    required
                    validate-on-blur
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
        <!-- TODO: Add Comment inputs -->
    </v-form>
</template>

<script>
import { mapState } from 'vuex'
export default {
    name: 'alter-table-opts',
    props: {
        data: { type: Object, required: true },
    },
    data() {
        return {
            tableInfo: {
                table_name: '',
                table_engine: '',
                table_charset: '',
                table_collation: '',
                table_comment: '',
                dbName: '', // no need to show this input
            },
            rules: {
                table_name: [
                    val => !!val || this.$t('errors.requiredInput', { inputName: this.$t('name') }),
                ],
                table_engine: [
                    val =>
                        !!val || this.$t('errors.requiredInput', { inputName: this.$t('engine') }),
                ],
                table_charset: [
                    val =>
                        !!val || this.$t('errors.requiredInput', { inputName: this.$t('charset') }),
                ],
                table_collation: [
                    val =>
                        !!val ||
                        this.$t('errors.requiredInput', { inputName: this.$t('collation') }),
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
        charsets() {
            return [...this.charset_collation_map.keys()]
        },
        collations() {
            return this.$typy(
                this.charset_collation_map.get(this.tableInfo.table_charset),
                'collations'
            ).safeArray
        },
        defCollation() {
            return this.$typy(
                this.charset_collation_map.get(this.tableInfo.table_charset),
                'defCollation'
            ).safeString
        },
        defDbCharset() {
            return this.$typy(this.def_db_charset_map.get(this.tableInfo.dbName)).safeString
        },
    },
    mounted() {
        this.tableInfo = this.data
    },
}
</script>
