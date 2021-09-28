<template>
    <v-card
        v-if="getLoadingTblCreationInfo"
        class="fill-height color border-top-table-border border-right-table-border border-bottom-table-border"
        :loading="getLoadingTblCreationInfo"
    />
    <div
        v-else
        class="relative fill-height color border-top-table-border border-right-table-border border-bottom-table-border"
    >
        <v-tooltip
            top
            transition="slide-y-transition"
            content-class="shadow-drop color text-navigation py-1 px-4"
        >
            <template v-slot:activator="{ on }">
                <v-btn class="close-ddl-editor-btn" icon small v-on="on" @click="closeDDLEditor">
                    <v-icon size="12" color="navigation"> $vuetify.icons.close</v-icon>
                </v-btn>
            </template>
            <span>{{ $t('closeDDLEditor') }}</span>
        </v-tooltip>

        <!-- TODO: Detach table functions to a dedicated component -->
        <div class="pa-4 pt-2">
            <!-- TODO: Replace below inputs with actual data -->
            <v-row>
                <v-col cols="12" md="6" class="">
                    <label class="field__label color text-small-text label-required">
                        {{ $t('name') }}
                    </label>
                    <v-text-field
                        id="table_name"
                        v-model="tableInfo.table_name"
                        :rules="rules.table_name"
                        name="table_name"
                        required
                        :height="32"
                        class="std error--text__bottom"
                        dense
                        outlined
                    />
                </v-col>
                <v-col cols="12" md="6" class="">
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
                    />
                </v-col>
            </v-row>
            <v-row>
                <v-col cols="12" md="6" class="">
                    <label class="field__label color text-small-text label-required">
                        {{ $t('charset') }}
                    </label>
                    <!-- TODO: Indicate default charset -->
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
                    />
                </v-col>
                <v-col cols="12" md="6" class="">
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
        </div>

        <v-tabs v-model="activeColSpec" :height="24" class="tab-navigation-wrapper">
            <v-tab color="primary" :href="`#${SQL_DDL_ALTER_SPECS.COLUMNS}`">
                <span> {{ $t('columns') }} </span>
            </v-tab>
            <v-tab color="primary" :href="`#${SQL_DDL_ALTER_SPECS.TRIGGERS}`">
                <span>{{ $t('triggers') }} </span>
            </v-tab>
        </v-tabs>
        <v-slide-x-transition>
            <keep-alive>
                <div v-if="activeColSpec === SQL_DDL_ALTER_SPECS.COLUMNS" class="px-4 py-2">
                    <!-- TODO: Replace with column functions -->
                    Alter column functions here
                </div>
                <div v-else-if="activeColSpec === SQL_DDL_ALTER_SPECS.TRIGGERS" class="px-4 py-2">
                    <!-- TODO: Replace with triggers functions -->
                    Alter triggers functions here
                </div>
            </keep-alive>
        </v-slide-x-transition>
    </div>
</template>

<script>
import { mapGetters, mapMutations, mapState } from 'vuex'
export default {
    name: 'ddl-editor',
    data() {
        return {
            tableInfo: {
                table_name: '',
                table_engine: '',
                table_charset: '',
                table_collation: '',
                table_comment: '',
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
            SQL_DDL_ALTER_SPECS: state => state.app_config.SQL_DDL_ALTER_SPECS,
            SQL_EDITOR_MODES: state => state.app_config.SQL_EDITOR_MODES,
            curr_ddl_alter_spec: state => state.query.curr_ddl_alter_spec,
            charset_collation_map: state => state.query.charset_collation_map,
            engines: state => state.query.engines,
        }),
        ...mapGetters({
            getLoadingTblCreationInfo: 'query/getLoadingTblCreationInfo',
            getTblCreationInfo: 'query/getTblCreationInfo',
        }),
        activeColSpec: {
            get() {
                return this.curr_ddl_alter_spec
            },
            set(value) {
                this.SET_CURR_DDL_COL_SPEC(value)
            },
        },
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
    },
    watch: {
        getTblCreationInfo(v) {
            const tblCreationInfo = v.table_info
            this.tableInfo = tblCreationInfo
        },
    },
    methods: {
        ...mapMutations({
            SET_CURR_DDL_COL_SPEC: 'query/SET_CURR_DDL_COL_SPEC',
            SET_CURR_EDITOR_MODE: 'query/SET_CURR_EDITOR_MODE',
        }),
        closeDDLEditor() {
            this.SET_CURR_EDITOR_MODE(this.SQL_EDITOR_MODES.TXT_EDITOR)
        },
    },
}
</script>

<style lang="scss" scoped>
.close-ddl-editor-btn {
    position: absolute;
    right: 8px;
    top: 4px;
}
</style>
