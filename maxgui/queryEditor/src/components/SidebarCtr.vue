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
                    @click="fetchSchemas"
                >
                    <template v-slot:btn-content>
                        <v-icon size="12" :color="reloadDisabled ? '' : 'deep-ocean'">
                            $vuetify.icons.mxs_reload
                        </v-icon>
                    </template>
                    {{ $mxs_t('reload') }}
                </mxs-tooltip-btn>
                <mxs-tooltip-btn
                    btnClass="toggle-sidebar"
                    icon
                    small
                    @click="SET_IS_SIDEBAR_COLLAPSED(!is_sidebar_collapsed)"
                >
                    <template v-slot:btn-content>
                        <v-icon
                            size="22"
                            color="deep-ocean"
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
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
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
import Worksheet from '@queryEditorSrc/store/orm/models/Worksheet'
import WorksheetMem from '@queryEditorSrc/store/orm/models/WorksheetMem'
import SchemaSidebar from '@queryEditorSrc/store/orm/models/SchemaSidebar'
import QueryTab from '@queryEditorSrc/store/orm/models/QueryTab'
import QueryConn from '@queryEditorSrc/store/orm/models/QueryConn'
import Editor from '@queryEditorSrc/store/orm/models/Editor'
import QueryResult from '@queryEditorSrc/store/orm/models/QueryResult'
import SchemaTreeCtr from './SchemaTreeCtr.vue'

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
            QUERY_MODES: state => state.queryEditorConfig.config.QUERY_MODES,
            NODE_TYPES: state => state.queryEditorConfig.config.NODE_TYPES,
            DDL_ALTER_SPECS: state => state.queryEditorConfig.config.DDL_ALTER_SPECS,
            EDITOR_MODES: state => state.queryEditorConfig.config.EDITOR_MODES,
            is_sidebar_collapsed: state => state.queryPersisted.is_sidebar_collapsed,
        }),
        activeWkeId() {
            return Worksheet.getters('getActiveWkeId')
        },
        activeQueryTabId() {
            return Worksheet.getters('getActiveQueryTabId')
        },
        filterTxt: {
            get() {
                return SchemaSidebar.getters('getFilterTxt')
            },
            set(v) {
                SchemaSidebar.update({ where: this.activeWkeId, data: { filter_txt: v } })
            },
        },
        isLoadingDbTree() {
            return SchemaSidebar.getters('getLoadingDbTree')
        },
        reloadDisabled() {
            return !this.hasConn || this.isLoadingDbTree
        },
        isSidebarDisabled() {
            return QueryConn.getters('getIsConnBusy') || this.isLoadingDbTree
        },
        hasConn() {
            return Boolean(this.$typy(QueryConn.getters('getActiveQueryTabConn'), 'id').safeString)
        },
    },
    methods: {
        ...mapMutations({
            SET_IS_SIDEBAR_COLLAPSED: 'queryPersisted/SET_IS_SIDEBAR_COLLAPSED',
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
                data: {
                    curr_query_mode: query_mode,
                },
            })
            await QueryResult.dispatch('fetchPrvw', { qualified_name: qualified_name, query_mode })
        },
        async handleLoadChildren(node) {
            await SchemaSidebar.dispatch('loadChildNodes', node)
        },
        async onAlterTable(node) {
            await QueryTab.dispatch('handleAddQueryTab', {
                worksheet_id: this.activeWkeId,
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
            await Worksheet.dispatch('exeStmtAction', {
                sql: this.execSqlDlg.sql,
                action: this.actionName,
            })
        },
        clearExeStatementsResult() {
            WorksheetMem.update({ where: this.activeWkeId, data: { exe_stmt_result: {} } })
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
