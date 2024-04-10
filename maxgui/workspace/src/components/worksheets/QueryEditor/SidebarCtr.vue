<template>
    <wke-sidebar
        v-model="isCollapsed"
        :title="$mxs_tc('schemas', 2)"
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
        <schema-tree-ctr
            v-show="!isCollapsed"
            class="schema-list-ctr"
            :queryEditorId="queryEditorId"
            :activeQueryTabId="activeQueryTabId"
            :queryEditorTmp="queryEditorTmp"
            :activeQueryTabConn="activeQueryTabConn"
            :schemaSidebar="schemaSidebar"
            :filterTxt="filterTxt"
            @get-node-data="fetchNodePrvwData"
            @load-children="handleLoadChildren"
            @use-db="useDb"
            @alter-tbl="onAlterTable"
            @drop-action="handleOpenExecSqlDlg"
            @truncate-tbl="handleOpenExecSqlDlg"
            @gen-erd="handleShowGenErdDlg([$event.qualified_name])"
            @view-node-insights="viewNodeInsights"
            v-on="$listeners"
        />
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
 * Change Date: 2028-04-03
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapState, mapActions, mapMutations } from 'vuex'
import InsightViewer from '@wsModels/InsightViewer'
import AlterEditor from '@wsModels/AlterEditor'
import QueryConn from '@wsModels/QueryConn'
import QueryResult from '@wsModels/QueryResult'
import QueryTab from '@wsModels/QueryTab'
import SchemaSidebar from '@wsModels/SchemaSidebar'
import Worksheet from '@wsModels/Worksheet'
import SchemaTreeCtr from '@wkeComps/QueryEditor/SchemaTreeCtr.vue'
import WkeSidebar from '@wkeComps/WkeSidebar.vue'
import schemaNodeHelper from '@wsSrc/utils/schemaNodeHelper'
import { NODE_TYPES, QUERY_TAB_TYPES } from '@wsSrc/constants'

export default {
    name: 'sidebar-ctr',
    components: { SchemaTreeCtr, WkeSidebar },
    props: {
        queryEditorId: { type: String, required: true },
        queryEditorTmp: { type: Object, required: true },
        activeQueryTabId: { type: String, required: true },
        activeQueryTabConn: { type: Object, required: true },
    },
    data() {
        return {
            actionName: '',
        }
    },
    computed: {
        ...mapState({
            is_sidebar_collapsed: state => state.prefAndStorage.is_sidebar_collapsed,
            exec_sql_dlg: state => state.mxsWorkspace.exec_sql_dlg,
        }),
        isCollapsed: {
            get() {
                return this.is_sidebar_collapsed
            },
            set(v) {
                this.SET_IS_SIDEBAR_COLLAPSED(v)
            },
        },
        schemaSidebar() {
            return SchemaSidebar.find(this.queryEditorId) || {}
        },
        filterTxt: {
            get() {
                return this.schemaSidebar.filter_txt || ''
            },
            set(v) {
                SchemaSidebar.update({ where: this.queryEditorId, data: { filter_txt: v } })
            },
        },
        activeQueryTabConnId() {
            return this.$typy(this.activeQueryTabConn, 'id').safeString
        },
        isLoadingDbTree() {
            return this.queryEditorTmp.loading_db_tree || false
        },
        hasConn() {
            return Boolean(this.activeQueryTabConnId)
        },
        disableReload() {
            return !this.hasConn || this.isLoadingDbTree
        },
        isSidebarDisabled() {
            return this.activeQueryTabConn.is_busy || this.isLoadingDbTree
        },
    },
    methods: {
        ...mapMutations({
            SET_IS_SIDEBAR_COLLAPSED: 'prefAndStorage/SET_IS_SIDEBAR_COLLAPSED',
            SET_GEN_ERD_DLG: 'mxsWorkspace/SET_GEN_ERD_DLG',
            SET_EXEC_SQL_DLG: 'mxsWorkspace/SET_EXEC_SQL_DLG',
        }),
        ...mapActions({
            queryDdlEditorSuppData: 'editorsMem/queryDdlEditorSuppData',
            exeStmtAction: 'mxsWorkspace/exeStmtAction',
        }),
        async fetchSchemas() {
            await SchemaSidebar.dispatch('fetchSchemas')
        },
        async useDb(schema) {
            await QueryConn.dispatch('useDb', {
                connName: this.$typy(this.activeQueryTabConn, 'meta.name').safeString,
                connId: this.activeQueryTabConnId,
                schema,
            })
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
        getSchemaIdentifier(node) {
            return this.$helpers.quotingIdentifier(schemaNodeHelper.getSchemaName(node))
        },
        async onAlterTable(node) {
            const config = Worksheet.getters('activeRequestConfig')
            await QueryTab.dispatch('handleAddQueryTab', {
                query_editor_id: this.queryEditorId,
                name: `ALTER ${node.name}`,
                type: QUERY_TAB_TYPES.ALTER_EDITOR,
                schema: this.getSchemaIdentifier(node),
            })
            await this.queryDdlEditorSuppData({ connId: this.activeQueryTabConnId, config })
            await AlterEditor.dispatch('queryTblCreationInfo', node)
        },

        handleOpenExecSqlDlg(sql) {
            this.SET_EXEC_SQL_DLG({
                ...this.exec_sql_dlg,
                is_opened: true,
                editor_height: 200,
                sql,
                on_exec: this.confirmExeStatements,
                after_cancel: this.clearExeStatementsResult,
            })
            this.actionName = sql.slice(0, -1)
        },
        async confirmExeStatements() {
            await this.exeStmtAction({
                connId: this.activeQueryTabConnId,
                sql: this.exec_sql_dlg.sql,
                action: this.actionName,
            })
        },
        clearExeStatementsResult() {
            this.SET_EXEC_SQL_DLG({ ...this.exec_sql_dlg, result: null })
        },
        handleShowGenErdDlg(preselectedSchemas) {
            this.SET_GEN_ERD_DLG({
                is_opened: true,
                preselected_schemas: this.$typy(preselectedSchemas).safeArray,
                connection:
                    QueryConn.query()
                        .where('query_editor_id', this.queryEditorId)
                        .first() || {},
                gen_in_new_ws: true,
            })
        },
        async viewNodeInsights(node) {
            let name = `Analyze ${node.name}`
            const { VIEW, TRIGGER, SP, FN } = NODE_TYPES
            switch (node.type) {
                case VIEW:
                case TRIGGER:
                case SP:
                case FN:
                    name = `Show create ${node.name} `
                    break
            }
            await QueryTab.dispatch('handleAddQueryTab', {
                query_editor_id: this.queryEditorId,
                name,
                type: QUERY_TAB_TYPES.INSIGHT_VIEWER,
                schema: this.getSchemaIdentifier(node),
            })
            InsightViewer.update({
                where: this.activeQueryTabId,
                data: { active_node: node },
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
