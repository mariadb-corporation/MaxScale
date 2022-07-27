<template>
    <div class="fill-height">
        <split-pane
            v-model="sidebarPct"
            class="query-view__content"
            :minPercent="minSidebarPct"
            split="vert"
            :disable="is_sidebar_collapsed"
            revertRender
        >
            <template slot="pane-left">
                <sidebar-ctr
                    :execSqlDlg.sync="execSqlDlg"
                    @place-to-editor="$typy($refs.txtEditor, 'placeToEditor').safeFunction($event)"
                    @on-dragging="$typy($refs.txtEditor, 'draggingTxt').safeFunction($event)"
                    @on-dragend="$typy($refs.txtEditor, 'dropTxtToEditor').safeFunction($event)"
                />
            </template>
            <template slot="pane-right">
                <div class="d-flex flex-column fill-height">
                    <div class="d-flex flex-column">
                        <session-tabs
                            :sessionToolbarRef="$typy($refs, 'sessionToolbar').safeObjectOrEmpty"
                        />
                        <!-- sessionToolbar ref is needed here so that its parent can call method in it  -->
                        <txt-editor-sess-toolbar v-if="getIsTxtEditor" ref="sessionToolbar" />
                    </div>
                    <keep-alive>
                        <txt-editor-ctr
                            v-if="getIsTxtEditor"
                            ref="txtEditor"
                            :dim="txtEditorDim"
                            v-on="$listeners"
                        />
                        <ddl-editor-ctr
                            v-else
                            ref="ddlEditor"
                            :dim="ddlEditorDim"
                            :execSqlDlg.sync="execSqlDlg"
                            :isExecFailed="isExecFailed"
                        />
                    </keep-alive>
                </div>
            </template>
        </split-pane>
        <execute-sql-dialog
            v-model="execSqlDlg.isOpened"
            :title="
                isExecFailed
                    ? $tc('errors.failedToExeStatements', stmtI18nPluralization)
                    : $tc('confirmations.exeStatements', stmtI18nPluralization)
            "
            :smallInfo="isExecFailed ? '' : $tc('info.exeStatementsInfo', stmtI18nPluralization)"
            :hasSavingErr="isExecFailed"
            :errMsgObj="stmtErrMsgObj"
            :sqlTobeExecuted.sync="execSqlDlg.sql"
            :editorHeight="execSqlDlg.editorHeight"
            :onSave="$typy(execSqlDlg, 'onExec').safeFunction"
            @after-close="$typy(execSqlDlg, 'onAfterClose').safeFunction()"
            @after-cancel="$typy(execSqlDlg, 'onAfterCancel').safeFunction()"
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
import { mapActions, mapGetters, mapState } from 'vuex'
import Sidebar from './Sidebar.container.vue'
import DDLEditor from './DDLEditor.container.vue'
import TxtEditor from './TxtEditor.container.vue'
import SessionTabs from './SessionTabs'
import TxtEditorSessToolbar from './TxtEditorSessToolbar'
import ExecuteSqlDialog from './ExecuteSqlDialog.vue'

export default {
    name: 'wke-ctr',
    components: {
        'sidebar-ctr': Sidebar,
        'txt-editor-ctr': TxtEditor,
        'ddl-editor-ctr': DDLEditor,
        'session-tabs': SessionTabs,
        'txt-editor-sess-toolbar': TxtEditorSessToolbar,
        'execute-sql-dialog': ExecuteSqlDialog,
    },
    props: {
        ctrDim: { type: Object, required: true },
    },
    data() {
        return {
            // split-pane states
            txtEditorDim: { width: 0, height: 0 },
            ddlEditorDim: { height: 0, width: 0 },
            sidebarPct: 0,
            execSqlDlg: {
                isOpened: false,
                editorHeight: 250,
                sql: '',
                onExec: () => null,
                onAfterClose: () => null,
                onAfterCancel: () => null,
            },
        }
    },
    computed: {
        ...mapState({
            SQL_EDITOR_MODES: state => state.app_config.SQL_EDITOR_MODES,
            is_sidebar_collapsed: state => state.schemaSidebar.is_sidebar_collapsed,
            curr_editor_mode: state => state.editor.curr_editor_mode,
            active_sql_conn: state => state.queryConn.active_sql_conn,
        }),
        ...mapGetters({
            getIsTxtEditor: 'editor/getIsTxtEditor',
            getExeStmtResultMap: 'schemaSidebar/getExeStmtResultMap',
        }),
        minSidebarPct() {
            if (this.is_sidebar_collapsed)
                return this.$help.pxToPct({ px: 40, containerPx: this.ctrDim.width })
            else return this.$help.pxToPct({ px: 200, containerPx: this.ctrDim.width })
        },
        stmtI18nPluralization() {
            const statementCounts = (this.execSqlDlg.sql.match(/;/g) || []).length
            return statementCounts > 1 ? 2 : 1
        },
        stmtErrMsgObj() {
            return this.$typy(this.getExeStmtResultMap, 'stmt_err_msg_obj').safeObjectOrEmpty
        },
        isExecFailed() {
            if (this.$typy(this.getExeStmtResultMap).isEmptyObject) return false
            return !this.$typy(this.stmtErrMsgObj).isEmptyObject
        },
    },
    watch: {
        sidebarPct(v) {
            if (v) this.$nextTick(() => this.handleRecalPanesDim())
        },
        ctrDim: {
            deep: true,
            handler(v, oV) {
                if (oV.height) this.$nextTick(() => this.handleRecalPanesDim())
            },
        },
        curr_editor_mode() {
            this.$nextTick(() => this.handleRecalPanesDim())
        },
    },
    created() {
        // handleSetSidebarPct should be called in doubleRAF to ensure all components are completely rendered
        this.$help.doubleRAF(() => {
            this.handleSetSidebarPct()
        })
    },
    activated() {
        this.watch_is_sidebar_collapsed()
        this.watch_active_sql_conn()
    },
    deactivated() {
        this.$typy(this.unwatch_is_sidebar_collapsed).safeFunction()
        this.$typy(this.unwatch_active_sql_conn).safeFunction()
    },
    methods: {
        ...mapActions({ handleInitialFetch: 'wke/handleInitialFetch' }),
        //Watchers to work with multiple worksheets which are kept alive
        watch_is_sidebar_collapsed() {
            this.unwatch_is_sidebar_collapsed = this.$watch('is_sidebar_collapsed', () =>
                this.handleSetSidebarPct()
            )
        },
        /**
         * A watcher on active_sql_conn state that is triggered immediately
         * to behave like a created hook. The watcher is watched/unwatched based on
         * activated/deactivated hook to prevent it from being triggered while changing
         * the value of active_sql_conn in another worksheet.
         */
        watch_active_sql_conn() {
            this.unwatch_active_sql_conn = this.$watch(
                'active_sql_conn',
                async () => await this.handleInitialFetch(),
                { deep: true, immediate: true }
            )
        },
        // panes dimension/percentages calculation functions
        handleSetSidebarPct() {
            if (this.is_sidebar_collapsed) this.sidebarPct = this.minSidebarPct
            else this.sidebarPct = this.$help.pxToPct({ px: 240, containerPx: this.ctrDim.width })
        },
        setTxtEditorPaneDim() {
            if (this.$refs.txtEditor.$el) {
                const { width, height } = this.$refs.txtEditor.$el.getBoundingClientRect()
                if (width !== 0 || height !== 0) this.txtEditorDim = { width, height }
            }
        },
        setDdlDim() {
            if (this.$refs.ddlEditor) {
                const { width, height } = this.$refs.ddlEditor.$el.getBoundingClientRect()
                if (width !== 0 || height !== 0) this.ddlEditorDim = { width, height }
            }
        },
        handleRecalPanesDim() {
            switch (this.curr_editor_mode) {
                case this.SQL_EDITOR_MODES.TXT_EDITOR:
                    this.setTxtEditorPaneDim()
                    break
                case this.SQL_EDITOR_MODES.DDL_EDITOR:
                    this.setDdlDim()
                    break
            }
        },
    },
}
</script>
