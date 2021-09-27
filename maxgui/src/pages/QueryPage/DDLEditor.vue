<template>
    <div
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

        <div class="pa-4 pt-2">
            <!-- TODO: Replace below inputs with actual data -->
            <v-row>
                <v-col cols="12" md="6" class="">
                    <label class="field__label color text-small-text label-required">
                        {{ $t('name') }}
                    </label>
                    <v-text-field
                        id="tableName"
                        v-model="tableName"
                        :rules="rules.tableName"
                        name="id"
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
                        v-model="engine"
                        :items="engines"
                        name="resourceName"
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
                        :rules="rules.engine"
                        required
                    />
                </v-col>
            </v-row>
            <!-- TODO: Add Charset/Collation and Comment inputs -->
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
import { mapMutations, mapState } from 'vuex'
export default {
    name: 'ddl-editor',
    data() {
        return {
            tableName: '',
            rules: {
                tableName: [
                    val => !!val || this.$t('errors.requiredInput', { inputName: this.$t('name') }),
                ],
                engine: [
                    val =>
                        !!val || this.$t('errors.requiredInput', { inputName: this.$t('engine') }),
                ],
            },
            engine: '',
            engines: [],
        }
    },
    computed: {
        ...mapState({
            SQL_DDL_ALTER_SPECS: state => state.app_config.SQL_DDL_ALTER_SPECS,
            SQL_EDITOR_MODES: state => state.app_config.SQL_EDITOR_MODES,
            curr_ddl_alter_spec: state => state.query.curr_ddl_alter_spec,
        }),
        activeColSpec: {
            get() {
                return this.curr_ddl_alter_spec
            },
            set(value) {
                this.SET_CURR_DDL_COL_SPEC(value)
            },
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
