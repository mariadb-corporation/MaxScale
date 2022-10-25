<template>
    <div
        class="sidebar-wrapper d-flex flex-column fill-height mxs-color-helper border-right-table-border"
        :class="{ 'not-allowed': isSidebarDisabled }"
    >
        <div class="sidebar-toolbar" :class="[is_sidebar_collapsed ? 'pa-1' : 'pa-3']">
            <div class="d-flex align-center justify-center">
                <span
                    v-if="!is_sidebar_collapsed"
                    class="mxs-color-helper text-small-text sidebar-toolbar__title d-inline-block text-truncate text-uppercase"
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
                @get-node-data="handleGetNodeData"
                @load-children="handleLoadChildren"
                @use-db="useDb"
                @alter-tbl="onAlterTable"
                @drop-action="onDropAction"
                @truncate-tbl="onTruncateTbl"
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
 * Change Date: 2026-10-04
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
            SQL_QUERY_MODES: state => state.queryEditorConfig.config.SQL_QUERY_MODES,
            SQL_NODE_TYPES: state => state.queryEditorConfig.config.SQL_NODE_TYPES,
            SQL_DDL_ALTER_SPECS: state => state.queryEditorConfig.config.SQL_DDL_ALTER_SPECS,
            SQL_EDITOR_MODES: state => state.queryEditorConfig.config.SQL_EDITOR_MODES,
            active_sql_conn: state => state.queryConn.active_sql_conn,
            is_sidebar_collapsed: state => state.queryPersisted.is_sidebar_collapsed,
            search_schema: state => state.schemaSidebar.search_schema,
            tbl_creation_info: state => state.editor.tbl_creation_info,
            active_wke_id: state => state.wke.active_wke_id,
        }),
        ...mapGetters({
            getLoadingDbTree: 'schemaSidebar/getLoadingDbTree',
            getIsConnBusy: 'queryConn/getIsConnBusy',
            getActiveSessionId: 'querySession/getActiveSessionId',
        }),
        filterTxt: {
            get() {
                return this.search_schema
            },
            set(v) {
                this.SET_SEARCH_SCHEMA({ payload: v, id: this.active_wke_id })
            },
        },
        reloadDisabled() {
            return !this.hasConn || this.getLoadingDbTree
        },
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
            SET_IS_SIDEBAR_COLLAPSED: 'queryPersisted/SET_IS_SIDEBAR_COLLAPSED',
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
            loadChildNodes: 'schemaSidebar/loadChildNodes',
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
            await this.loadChildNodes(node)
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
