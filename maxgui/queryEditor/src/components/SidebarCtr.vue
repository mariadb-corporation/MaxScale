<template>
    <sidebar
        :disabled="isSidebarDisabled"
        :isCollapsed="is_sidebar_collapsed"
        :hasConn="hasConn"
        :isLoading="getLoadingDbTree"
        :searchSchema="search_schema"
        @set-search-schema="SET_SEARCH_SCHEMA({ payload: $event, id: active_wke_id })"
        @reload-schemas="fetchSchemas"
        @toggle-sidebar="
            SET_IS_SIDEBAR_COLLAPSED({
                payload: !is_sidebar_collapsed,
                id: active_wke_id,
            })
        "
        @get-node-data="handleGetNodeData"
        @load-children="handleLoadChildren"
        @use-db="useDb"
        @alter-tbl="onAlterTable"
        @drop-action="onDropAction"
        @truncate-tbl="onTruncateTbl"
        v-on="$listeners"
    />
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
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
import { mapState, mapActions, mapMutations, mapGetters } from 'vuex'
import Sidebar from './Sidebar.vue'
export default {
    name: 'sidebar-ctr',
    components: { Sidebar },
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
            SQL_QUERY_MODES: state => state.queryEditorConfig.config.SQL_QUERY_MODES,
            SQL_NODE_TYPES: state => state.queryEditorConfig.config.SQL_NODE_TYPES,
            SQL_DDL_ALTER_SPECS: state => state.queryEditorConfig.config.SQL_DDL_ALTER_SPECS,
            SQL_EDITOR_MODES: state => state.queryEditorConfig.config.SQL_EDITOR_MODES,
            active_sql_conn: state => state.queryConn.active_sql_conn,
            is_sidebar_collapsed: state => state.schemaSidebar.is_sidebar_collapsed,
            search_schema: state => state.schemaSidebar.search_schema,
            tbl_creation_info: state => state.editor.tbl_creation_info,
            active_wke_id: state => state.wke.active_wke_id,
        }),
        ...mapGetters({
            getLoadingDbTree: 'schemaSidebar/getLoadingDbTree',
            getIsConnBusy: 'queryConn/getIsConnBusy',
            getActiveSessionId: 'querySession/getActiveSessionId',
        }),
        isSidebarDisabled() {
            return this.getIsConnBusy && !this.getLoadingDbTree
        },
        hasConn() {
            return Boolean(this.$typy(this.active_sql_conn, 'id').safeString)
        },
    },
    methods: {
        ...mapMutations({
            SET_SEARCH_SCHEMA: 'schemaSidebar/SET_SEARCH_SCHEMA',
            SET_IS_SIDEBAR_COLLAPSED: 'schemaSidebar/SET_IS_SIDEBAR_COLLAPSED',
            PATCH_EXE_STMT_RESULT_MAP: 'schemaSidebar/PATCH_EXE_STMT_RESULT_MAP',
            SET_CURR_QUERY_MODE: 'queryResult/SET_CURR_QUERY_MODE',
            SET_TBL_CREATION_INFO: 'editor/SET_TBL_CREATION_INFO',
            SET_CURR_EDITOR_MODE: 'editor/SET_CURR_EDITOR_MODE',
            SET_CURR_DDL_ALTER_SPEC: 'editor/SET_CURR_DDL_ALTER_SPEC',
        }),
        ...mapActions({
            fetchSchemas: 'schemaSidebar/fetchSchemas',
            clearDataPreview: 'queryResult/clearDataPreview',
            fetchPrvw: 'queryResult/fetchPrvw',
            updateTreeNodes: 'schemaSidebar/updateTreeNodes',
            useDb: 'queryConn/useDb',
            queryAlterTblSuppData: 'editor/queryAlterTblSuppData',
            queryTblCreationInfo: 'editor/queryTblCreationInfo',
            exeStmtAction: 'schemaSidebar/exeStmtAction',
            handleAddNewSession: 'querySession/handleAddNewSession',
        }),

        async handleGetNodeData({ SQL_QUERY_MODE, schemaId }) {
            this.clearDataPreview()
            this.SET_CURR_QUERY_MODE({ payload: SQL_QUERY_MODE, id: this.getActiveSessionId })
            switch (SQL_QUERY_MODE) {
                case this.SQL_QUERY_MODES.PRVW_DATA:
                case this.SQL_QUERY_MODES.PRVW_DATA_DETAILS:
                    await this.fetchPrvw({
                        tblId: schemaId,
                        prvwMode: SQL_QUERY_MODE,
                    })
                    break
            }
        },
        async handleLoadChildren(node) {
            await this.updateTreeNodes(node)
        },
        async onAlterTable(node) {
            await this.handleAddNewSession({
                wke_id: this.active_wke_id,
                name: `ALTER ${node.name}`,
            })
            this.SET_TBL_CREATION_INFO({
                id: this.getActiveSessionId,
                payload: { ...this.tbl_creation_info, altered_active_node: node },
            })
            this.SET_CURR_EDITOR_MODE({
                id: this.getActiveSessionId,
                payload: this.SQL_EDITOR_MODES.DDL_EDITOR,
            })
            this.SET_CURR_DDL_ALTER_SPEC({
                payload: this.SQL_DDL_ALTER_SPECS.COLUMNS,
                id: this.getActiveSessionId,
            })
            await this.queryAlterTblSuppData()
            await this.queryTblCreationInfo(node)
        },
        /**
         * @param {String} payload.id - identifier
         * @param {String} payload.type - db tree node type
         */
        onDropAction({ id, type }) {
            const { escapeIdentifiers: escape } = this.$helpers
            let sql = 'DROP'
            const { SCHEMA, TABLE, SP, TRIGGER } = this.SQL_NODE_TYPES
            switch (type) {
                case SCHEMA:
                    sql += ' SCHEMA'
                    break
                case TABLE:
                    sql += ' TABLE'
                    break
                case SP:
                    sql += ' PROCEDURE'
                    break
                case TRIGGER:
                    sql += ' TRIGGER'
                    break
            }
            sql = `${sql} ${escape(id)};`
            this.handleOpenExecSqlDlg(sql)
        },

        /**
         * @param {String} id - identifier
         */
        onTruncateTbl(id) {
            const { escapeIdentifiers: escape } = this.$helpers
            const sql = `truncate ${escape(id)};`
            this.handleOpenExecSqlDlg(sql)
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
            await this.exeStmtAction({ sql: this.execSqlDlg.sql, action: this.actionName })
        },
        clearExeStatementsResult() {
            this.PATCH_EXE_STMT_RESULT_MAP({ id: this.active_wke_id })
        },
    },
}
</script>
