<template>
    <v-card
        v-if="getLoadingTblCreationInfo || !tableInfo"
        class="fill-height color border-top-table-border border-right-table-border border-bottom-table-border"
        :loading="Boolean(getLoadingTblCreationInfo && !tableInfo)"
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

        <alter-table-opts :data="tableInfo" />

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
import AlterTableOpts from './AlterTableOpts.vue'
export default {
    name: 'ddl-editor',
    components: {
        'alter-table-opts': AlterTableOpts,
    },
    computed: {
        ...mapState({
            SQL_DDL_ALTER_SPECS: state => state.app_config.SQL_DDL_ALTER_SPECS,
            SQL_EDITOR_MODES: state => state.app_config.SQL_EDITOR_MODES,
            curr_ddl_alter_spec: state => state.query.curr_ddl_alter_spec,
        }),
        ...mapGetters({
            getLoadingTblCreationInfo: 'query/getLoadingTblCreationInfo',
            getTblCreationInfo: 'query/getTblCreationInfo',
            getActiveTreeNode: 'query/getActiveTreeNode',
        }),
        activeColSpec: {
            get() {
                return this.curr_ddl_alter_spec
            },
            set(value) {
                this.SET_CURR_DDL_COL_SPEC(value)
            },
        },
        tableInfo() {
            return this.$typy(this.getTblCreationInfo, 'table_info').safeObject
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
