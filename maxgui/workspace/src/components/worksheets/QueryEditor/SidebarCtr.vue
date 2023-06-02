<template>
    <wke-sidebar
        v-model="isCollapsed"
        :title="$mxs_t('schemas')"
        :class="{ 'not-allowed': isSidebarDisabled }"
    >
        <template v-slot:collapse-btn-prepend>
            <mxs-tooltip-btn
                btnClass="visualize-schemas"
                icon
                small
                :disabled="isSidebarDisabled || !hasConn"
                :color="isSidebarDisabled ? '' : 'primary'"
                @click="handleShowGenErdDlg()"
            >
                <template v-slot:btn-content>
                    <v-icon size="12">
                        $vuetify.icons.mxs_reports
                    </v-icon>
                </template>
                {{ $mxs_t('genErd') }}
            </mxs-tooltip-btn>
            <mxs-tooltip-btn
                btnClass="reload-schemas"
                icon
                small
                :disabled="disableReload"
                :color="disableReload ? '' : 'primary'"
                @click="fetchSchemas"
            >
                <template v-slot:btn-content>
                    <v-icon size="12">
                        $vuetify.icons.mxs_reload
                    </v-icon>
                </template>
                {{ $mxs_t('reload') }}
            </mxs-tooltip-btn>
        </template>
        <template v-slot:toolbar-append>
            <v-text-field
                v-model="filterTxt"
                name="searchSchema"
                dense
                outlined
                height="28"
                class="vuetify-input--override filter-objects"
                :placeholder="$mxs_t('filterSchemaObjects')"
                :disabled="!hasConn"
            />
        </template>
        <keep-alive>
            <schema-tree-ctr
                v-show="!isCollapsed"
                class="schema-list-ctr"
                @get-node-data="fetchNodePrvwData"
                @load-children="handleLoadChildren"
                @use-db="useDb"
                @alter-tbl="onAlterTable"
                @drop-action="handleOpenExecSqlDlg"
                @truncate-tbl="handleOpenExecSqlDlg"
                @gen-erd="handleShowGenErdDlg([$event.qualified_name])"
                v-on="$listeners"
            />
        </keep-alive>
    </wke-sidebar>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-05-22
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
import WkeSidebar from '@wkeComps/WkeSidebar.vue'

export default {
    name: 'sidebar-ctr',
    components: { SchemaTreeCtr, WkeSidebar },
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
        isCollapsed: {
            get() {
                return this.is_sidebar_collapsed
            },
            set(v) {
                this.SET_IS_SIDEBAR_COLLAPSED(v)
            },
        },
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
        disableReload() {
            return !this.hasConn || this.isLoadingDbTree
        },
        isSidebarDisabled() {
            return QueryConn.getters('getIsActiveQueryTabConnBusy') || this.isLoadingDbTree
        },
        hasConn() {
            return Boolean(this.$typy(QueryConn.getters('getActiveQueryTabConn'), 'id').safeString)
        },
        dbTreeData() {
            return SchemaSidebar.getters('getDbTreeData')
        },
        activeQueryEditorConn() {
            return QueryConn.getters('getQueryEditorConn')
        },
    },
    methods: {
        ...mapMutations({
            SET_IS_SIDEBAR_COLLAPSED: 'prefAndStorage/SET_IS_SIDEBAR_COLLAPSED',
            SET_GEN_ERD_DLG: 'mxsWorkspace/SET_GEN_ERD_DLG',
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

            Editor.update({
                where: this.activeQueryTabId,
                data(editor) {
                    editor.curr_editor_mode = mode
                    editor.tbl_creation_info.altering_node = node
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
        handleShowGenErdDlg(preselectedSchemas) {
            this.SET_GEN_ERD_DLG({
                is_opened: true,
                preselected_schemas: this.$typy(preselectedSchemas).safeArray,
                connection: this.activeQueryEditorConn,
                gen_in_new_ws: true,
            })
        },
    },
}
</script>
<style lang="scss" scoped>
.schema-list-ctr {
    font-size: 12px;
    overflow-y: auto;
    z-index: 1;
}
</style>

<style lang="scss">
.vuetify-input--override.filter-objects {
    input {
        font-size: 12px !important;
    }
}
</style>
