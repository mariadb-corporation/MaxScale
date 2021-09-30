<template>
    <v-card
        v-if="getLoadingTblCreationInfo || !initialTblOptsData"
        class="fill-height color border-top-table-border border-right-table-border border-bottom-table-border"
        :loading="Boolean(getLoadingTblCreationInfo && !initialTblOptsData)"
    />
    <div
        v-else
        class="relative fill-height color border-top-table-border border-right-table-border border-bottom-table-border"
    >
        <div class="pt-2 pl-3 pr-2 d-flex align-center justify-space-between">
            <span class="text-body-2 color text-navigation font-weight-bold text-uppercase">
                {{ $t('alterTbl') }}
            </span>
            <v-tooltip
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
            >
                <template v-slot:activator="{ on }">
                    <v-btn icon small v-on="on" @click="closeDDLEditor">
                        <v-icon size="12" color="navigation"> $vuetify.icons.close</v-icon>
                    </v-btn>
                </template>
                <span>{{ $t('closeDDLEditor') }}</span>
            </v-tooltip>
        </div>
        <portal to="wke-toolbar-right">
            <alter-table-toolbar
                :disableRevert="!hasChanged"
                :disableApply="!hasValidChanges"
                @on-revert="revertChanges"
                @on-apply="applyChanges"
            />
        </portal>
        <v-form v-model="isFormValid">
            <alter-table-opts v-model="tableOptsData" />
            <v-tabs v-model="activeColSpec" :height="24" class="tab-navigation-wrapper">
                <v-tab color="primary" :href="`#${SQL_DDL_ALTER_SPECS.COLUMNS}`">
                    <span> {{ $t('columns') }} </span>
                </v-tab>
                <v-tab color="primary" :href="`#${SQL_DDL_ALTER_SPECS.TRIGGERS}`">
                    <span>{{ $t('triggers') }} </span>
                </v-tab>
                <v-tabs-items v-model="activeColSpec">
                    <v-tab-item
                        v-for="spec in SQL_DDL_ALTER_SPECS"
                        :id="spec"
                        :key="spec"
                        class="pt-2"
                    >
                        <div v-if="activeColSpec === spec" class="px-4 py-2">
                            <!-- TODO: Replace with columns/triggers input specs -->
                            {{ spec }} input specs here
                        </div>
                    </v-tab-item>
                </v-tabs-items>
            </v-tabs>
        </v-form>
    </div>
</template>

<script>
import { mapGetters, mapMutations, mapState } from 'vuex'
import AlterTableOpts from './AlterTableOpts.vue'
import AlterTableToolbar from './AlterTableToolbar.vue'
export default {
    name: 'ddl-editor',
    components: {
        'alter-table-opts': AlterTableOpts,
        'alter-table-toolbar': AlterTableToolbar,
    },
    data() {
        return {
            isFormValid: true,
            tableOptsData: {},
        }
    },
    computed: {
        ...mapState({
            SQL_DDL_ALTER_SPECS: state => state.app_config.SQL_DDL_ALTER_SPECS,
            SQL_EDITOR_MODES: state => state.app_config.SQL_EDITOR_MODES,
            curr_ddl_alter_spec: state => state.query.curr_ddl_alter_spec,
            active_wke_id: state => state.query.active_wke_id,
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
        /**
         * TODO: Add more inputs to initialData and newData
         * All inputs will be stored in an object,
         * for now only table option inputs (tableOptsData) here.
         */
        initialData() {
            return { ...this.initialTblOptsData }
        },
        newData() {
            return { ...this.tableOptsData }
        },
        initialTblOptsData() {
            return this.$typy(this.getTblCreationInfo, 'table_opts_data').safeObject
        },
        hasChanged() {
            return !this.$help.lodash.isEqual(this.initialData, this.newData)
        },
        hasValidChanges() {
            return this.isFormValid && this.hasChanged
        },
    },
    watch: {
        initialTblOptsData: {
            deep: true,
            handler(v) {
                this.tableOptsData = this.$help.lodash.cloneDeep(v)
            },
        },
    },
    methods: {
        ...mapMutations({
            SET_CURR_DDL_COL_SPEC: 'query/SET_CURR_DDL_COL_SPEC',
            SET_CURR_EDITOR_MODE: 'query/SET_CURR_EDITOR_MODE',
            UPDATE_TBL_CREATION_INFO_MAP: 'query/UPDATE_TBL_CREATION_INFO_MAP',
        }),
        closeDDLEditor() {
            // Clear altered active node
            this.UPDATE_TBL_CREATION_INFO_MAP({
                id: this.active_wke_id,
                payload: {
                    altered_active_node: null,
                },
            })
            this.SET_CURR_EDITOR_MODE(this.SQL_EDITOR_MODES.TXT_EDITOR)
        },
        revertChanges() {
            this.tableOptsData = this.$help.lodash.cloneDeep(this.initialTblOptsData)
        },
        applyChanges() {
            //TODO: Pick only keys have value changes and build SQL alter statements here
        },
    },
}
</script>
