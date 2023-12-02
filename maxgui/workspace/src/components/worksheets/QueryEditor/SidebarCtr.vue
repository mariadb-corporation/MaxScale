<template>
    <div
        class="sidebar-wrapper d-flex flex-column fill-height mxs-color-helper border-right-table-border"
        :class="{ 'not-allowed': isSidebarDisabled }"
    >
        <div class="sidebar-toolbar" :class="[is_sidebar_collapsed ? 'pa-1' : 'pa-3']">
            <div class="d-flex align-center justify-center">
                <span
                    v-if="!is_sidebar_collapsed"
                    class="mxs-color-helper text-small-text sidebar-toolbar__title d-inline-block text-truncate text-capitalize"
                >
                    {{ $mxs_t('schemas') }}
                </span>
                <mxs-tooltip-btn
                    v-if="!is_sidebar_collapsed"
                    btnClass="reload-schemas"
                    icon
                    small
                    :disabled="reloadDisabled"
                    :color="reloadDisabled ? '' : 'primary'"
                    @click="fetchSchemas"
                >
                    <template v-slot:btn-content>
                        <v-icon size="12">
                            $vuetify.icons.mxs_reload
                        </v-icon>
                    </template>
                    {{ $mxs_t('reload') }}
                </mxs-tooltip-btn>
                <mxs-tooltip-btn
                    btnClass="toggle-sidebar"
                    icon
                    small
                    color="primary"
                    @click="SET_IS_SIDEBAR_COLLAPSED(!is_sidebar_collapsed)"
                >
                    <template v-slot:btn-content>
                        <v-icon
                            size="22"
                            class="collapse-icon"
                            :class="[is_sidebar_collapsed ? 'rotate-right' : 'rotate-left']"
                        >
                            mdi-chevron-double-down
                        </v-icon>
                    </template>
                    {{ is_sidebar_collapsed ? $mxs_t('expand') : $mxs_t('collapse') }}
                </mxs-tooltip-btn>
            </div>
            <v-text-field
                v-if="!is_sidebar_collapsed"
                v-model="filterTxt"
                name="searchSchema"
                dense
                outlined
                height="28"
                class="vuetify-input--override filter-objects"
                :placeholder="$mxs_t('filterSchemaObjects')"
                :disabled="!hasConn"
            />
        </div>
        <keep-alive>
            <schema-tree-ctr
                v-show="!is_sidebar_collapsed"
                class="schema-list-ctr"
                @get-node-data="fetchNodePrvwData"
                @load-children="handleLoadChildren"
                @use-db="useDb"
                @alter-tbl="onAlterTable"
                @drop-action="handleOpenExecSqlDlg"
                @truncate-tbl="handleOpenExecSqlDlg"
                v-on="$listeners"
            />
        </keep-alive>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

/*
 * Events
 * 2-way data binding to execSqlDlg prop
 * update:execSqlDlg?: (object)
 */
import { mapState, mapActions, mapMutations } from 'vuex'
import Editor from '@wsModels/Editor'
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import QueryEditorTmp from '@wsModels/QueryEditorTmp'
import QueryResult from '@wsModels/QueryResult'
import QueryTab from '@wsModels/QueryTab'
import SchemaSidebar from '@wsModels/SchemaSidebar'
import SchemaTreeCtr from '@wkeComps/QueryEditor/SchemaTreeCtr.vue'

export default {
    name: 'sidebar-ctr',
    components: { SchemaTreeCtr },
    props: {
        execSqlDlg: { type: Object, required: true },
    },
    data() {
        return {
            actionName: '',
        }
    },
    computed: {
        ...mapState({
            QUERY_MODES: state => state.mxsWorkspace.config.QUERY_MODES,
            NODE_TYPES: state => state.mxsWorkspace.config.NODE_TYPES,
            DDL_ALTER_SPECS: state => state.mxsWorkspace.config.DDL_ALTER_SPECS,
            EDITOR_MODES: state => state.mxsWorkspace.config.EDITOR_MODES,
            is_sidebar_collapsed: state => state.prefAndStorage.is_sidebar_collapsed,
        }),
        queryEditorId() {
            return QueryEditor.getters('getQueryEditorId')
        },
        activeQueryTabId() {
            return QueryEditor.getters('getActiveQueryTabId')
        },
        filterTxt: {
            get() {
                return SchemaSidebar.getters('getFilterTxt')
            },
            set(v) {
                SchemaSidebar.update({ where: this.queryEditorId, data: { filter_txt: v } })
            },
        },
        isLoadingDbTree() {
            return SchemaSidebar.getters('getLoadingDbTree')
        },
        reloadDisabled() {
            return !this.hasConn || this.isLoadingDbTree
        },
        isSidebarDisabled() {
            return QueryConn.getters('getIsActiveQueryTabConnBusy') || this.isLoadingDbTree
        },
        hasConn() {
            return Boolean(this.$typy(QueryConn.getters('getActiveQueryTabConn'), 'id').safeString)
        },
    },
    methods: {
        ...mapMutations({
            SET_IS_SIDEBAR_COLLAPSED: 'prefAndStorage/SET_IS_SIDEBAR_COLLAPSED',
        }),
        ...mapActions({
            queryAlterTblSuppData: 'editorsMem/queryAlterTblSuppData',
        }),
        async fetchSchemas() {
            await SchemaSidebar.dispatch('fetchSchemas')
        },
        async useDb(param) {
            await QueryConn.dispatch('useDb', param)
        },
        async fetchNodePrvwData({ query_mode, qualified_name }) {
            QueryResult.dispatch('clearDataPreview')
            QueryResult.update({
                where: this.activeQueryTabId,
                data: { query_mode },
            })
            await QueryResult.dispatch('fetchPrvw', { qualified_name: qualified_name, query_mode })
        },
        async handleLoadChildren(node) {
            await SchemaSidebar.dispatch('loadChildNodes', node)
        },
        async onAlterTable(node) {
            await QueryTab.dispatch('handleAddQueryTab', {
                query_editor_id: this.queryEditorId,
                name: `ALTER ${node.name}`,
            })
            const mode = this.EDITOR_MODES.DDL_EDITOR
            const spec = this.DDL_ALTER_SPECS.COLUMNS
            Editor.update({
                where: this.activeQueryTabId,
                data(editor) {
                    editor.curr_editor_mode = mode
                    editor.curr_ddl_alter_spec = spec
                    editor.tbl_creation_info.altered_active_node = node
                },
            })
            await this.queryAlterTblSuppData()
            await Editor.dispatch('queryTblCreationInfo', node)
        },

        handleOpenExecSqlDlg(sql) {
            this.$emit('update:execSqlDlg', {
                ...this.execSqlDlg,
                isOpened: true,
                editorHeight: 200,
                sql,
                onExec: this.confirmExeStatements,
                onAfterClose: this.clearExeStatementsResult,
                onAfterCancel: this.clearExeStatementsResult,
            })
            this.actionName = sql.slice(0, -1)
        },
        async confirmExeStatements() {
            await QueryEditor.dispatch('exeStmtAction', {
                sql: this.execSqlDlg.sql,
                action: this.actionName,
            })
        },
        clearExeStatementsResult() {
            QueryEditorTmp.update({ where: this.queryEditorId, data: { exe_stmt_result: {} } })
        },
    },
}
</script>
<style lang="scss" scoped>
.sidebar-wrapper {
    width: 100%;
    .sidebar-toolbar {
        height: 60px;
        padding-top: 2px !important;
        &__title {
            font-size: 12px;
            margin-right: auto;
        }
    }
    .schema-list-ctr {
        font-size: 12px;
        overflow-y: auto;
        z-index: 1;
    }
}
</style>

<style lang="scss">
.vuetify-input--override.filter-objects {
    input {
        font-size: 12px !important;
    }
}
</style>
