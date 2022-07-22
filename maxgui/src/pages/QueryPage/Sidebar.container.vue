<template>
    <div class="fill-height">
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
        <execute-sql-dialog
            v-model="isExeDlgOpened"
            :title="
                isExeStatementsFailed
                    ? $tc('errors.failedToExeStatements', stmtI18nPluralization)
                    : $tc('confirmations.exeStatements', stmtI18nPluralization)
            "
            :smallInfo="
                isExeStatementsFailed ? '' : $tc('info.exeStatementsInfo', stmtI18nPluralization)
            "
            :hasSavingErr="isExeStatementsFailed"
            :errMsgObj="stmtErrMsgObj"
            :sqlTobeExecuted.sync="sql"
            :editorHeight="200"
            :onSave="confirmExeStatements"
            @after-close="clearExeStatementsResult"
            @after-cancel="clearExeStatementsResult"
        />
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-07-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { mapState, mapActions, mapMutations, mapGetters } from 'vuex'
import ExecuteSqlDialog from './ExecuteSqlDialog.vue'
import Sidebar from './Sidebar.vue'
export default {
    name: 'sidebar-ctr',
    components: {
        Sidebar,
        'execute-sql-dialog': ExecuteSqlDialog,
    },
    data() {
        return {
            // execute-sql-dialog states
            isExeDlgOpened: false,
            sql: '',
            actionName: '',
        }
    },
    computed: {
        ...mapState({
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
            SQL_NODE_TYPES: state => state.app_config.SQL_NODE_TYPES,
            SQL_DDL_ALTER_SPECS: state => state.app_config.SQL_DDL_ALTER_SPECS,
            SQL_EDITOR_MODES: state => state.app_config.SQL_EDITOR_MODES,
            active_sql_conn: state => state.queryConn.active_sql_conn,
            is_sidebar_collapsed: state => state.schemaSidebar.is_sidebar_collapsed,
            search_schema: state => state.schemaSidebar.search_schema,
            engines: state => state.editor.engines,
            charset_collation_map: state => state.editor.charset_collation_map,
            def_db_charset_map: state => state.editor.def_db_charset_map,
            tbl_creation_info: state => state.editor.tbl_creation_info,
            active_wke_id: state => state.wke.active_wke_id,
        }),
        ...mapGetters({
            getLoadingDbTree: 'schemaSidebar/getLoadingDbTree',
            getIsConnBusy: 'queryConn/getIsConnBusy',
            getExeStmtResultMap: 'schemaSidebar/getExeStmtResultMap',
            getActiveSessionId: 'querySession/getActiveSessionId',
            getDbTreeData: 'schemaSidebar/getDbTreeData',
            getCurrDbTree: 'schemaSidebar/getCurrDbTree',
        }),
        isSidebarDisabled() {
            return this.getIsConnBusy && !this.getLoadingDbTree
        },
        hasConn() {
            return Boolean(this.$typy(this.active_sql_conn, 'id').safeString)
        },
        stmtI18nPluralization() {
            const statementCounts = (this.sql.match(/;/g) || []).length
            return statementCounts > 1 ? 2 : 1
        },
        isExeStatementsFailed() {
            if (this.$typy(this.getExeStmtResultMap).isEmptyObject) return false
            return !this.$typy(this.stmtErrMsgObj).isEmptyObject
        },
        stmtErrMsgObj() {
            return this.$typy(this.getExeStmtResultMap, 'stmt_err_msg_obj').safeObjectOrEmpty
        },
    },
    deactivated() {
        this.$typy(this.unwatch_active_sql_conn).safeFunction()
    },
    activated() {
        this.watch_active_sql_conn()
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
            useDb: 'schemaSidebar/useDb',
            queryTblCreationInfo: 'editor/queryTblCreationInfo',
            queryCharsetCollationMap: 'editor/queryCharsetCollationMap',
            queryEngines: 'editor/queryEngines',
            queryDefDbCharsetMap: 'editor/queryDefDbCharsetMap',
            exeStmtAction: 'schemaSidebar/exeStmtAction',
            initialFetch: 'schemaSidebar/initialFetch',
            handleAddNewSession: 'querySession/handleAddNewSession',
        }),
        /**
         * A watcher on active_sql_conn state that is triggered immediately
         * to behave like a created hook.
         * It calls initialFetch to populate the data if the worksheet has an
         * active connection but schema tree data is an empty array
         * or when the new connection is being created, chosen or when changing
         * the worksheet
         */
        watch_active_sql_conn() {
            this.unwatch_active_sql_conn = this.$watch(
                'active_sql_conn',
                async v => {
                    if (
                        this.getDbTreeData.length === 0 ||
                        this.$typy(this.getCurrDbTree, 'data_of_conn').safeString !== v.name
                    )
                        await this.initialFetch(v)
                },
                { deep: true, immediate: true }
            )
        },
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
            //Query once as the data won't be changed
            if (this.$typy(this.engines).isEmptyArray) await this.queryEngines()
            if (this.$typy(this.charset_collation_map).isEmptyObject)
                await this.queryCharsetCollationMap()
            if (this.$typy(this.def_db_charset_map).isEmptyObject) await this.queryDefDbCharsetMap()
            await this.queryTblCreationInfo(node)
        },
        /**
         * @param {String} payload.id - identifier
         * @param {String} payload.type - db tree node type
         */
        onDropAction({ id, type }) {
            const { escapeIdentifiers: escape } = this.$help
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
            this.sql = `${sql} ${escape(id)};`
            this.actionName = this.sql.slice(0, -1)
            this.isExeDlgOpened = true
        },
        /**
         * @param {String} id - identifier
         */
        onTruncateTbl(id) {
            const { escapeIdentifiers: escape } = this.$help
            this.sql = `truncate ${escape(id)};`
            this.actionName = this.sql.slice(0, -1)
            this.isExeDlgOpened = true
        },
        async confirmExeStatements() {
            await this.exeStmtAction({ sql: this.sql, action: this.actionName })
        },
        clearExeStatementsResult() {
            this.PATCH_EXE_STMT_RESULT_MAP({ id: this.active_wke_id })
        },
    },
}
</script>
