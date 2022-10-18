<template>
    <div class="fill-height">
        <mxs-split-pane
            :value="sidebarPct"
            class="query-view__content"
            :minPercent="minSidebarPct"
            split="vert"
            progress
            revertRender
            @resizing="onResizing"
        >
            <template slot="pane-left">
                <sidebar-ctr
                    :execSqlDlg.sync="execSqlDlg"
                    @place-to-editor="$typy($refs, 'editor[0].placeToEditor').safeFunction($event)"
                    @on-dragging="$typy($refs, 'editor[0].draggingTxt').safeFunction($event)"
                    @on-dragend="$typy($refs, 'editor[0].dropTxtToEditor').safeFunction($event)"
                />
            </template>
            <template slot="pane-right">
                <div class="d-flex flex-column fill-height">
                    <session-nav-ctr :height="sessTabCtrHeight" />
                    <keep-alive v-for="session in query_sessions" :key="session.id" :max="20">
                        <template v-if="getActiveSessionId === session.id">
                            <txt-editor-ctr
                                v-if="getIsTxtEditor"
                                ref="editor"
                                :session="session"
                                :dim="editorDim"
                            >
                                <slot v-for="(_, slot) in $slots" :slot="slot" :name="slot" />
                            </txt-editor-ctr>
                            <ddl-editor-ctr
                                v-else
                                ref="editor"
                                :dim="editorDim"
                                :execSqlDlg.sync="execSqlDlg"
                                :isExecFailed="isExecFailed"
                            >
                                <slot v-for="(_, slot) in $slots" :slot="slot" :name="slot" />
                            </ddl-editor-ctr>
                        </template>
                    </keep-alive>
                    <file-dlg-ctr />
                </div>
            </template>
        </mxs-split-pane>
        <execute-sql-dialog
            v-model="execSqlDlg.isOpened"
            :title="
                isExecFailed
                    ? $mxs_tc('errors.failedToExeStatements', stmtI18nPluralization)
                    : $mxs_tc('confirmations.exeStatements', stmtI18nPluralization)
            "
            :smallInfo="
                isExecFailed ? '' : $mxs_tc('info.exeStatementsInfo', stmtI18nPluralization)
            "
            :hasSavingErr="isExecFailed"
            :errMsgObj="stmtErrMsgObj"
            :sqlTobeExecuted.sync="execSqlDlg.sql"
            :editorHeight="execSqlDlg.editorHeight"
            :dbCmplList="getDbCmplList"
            :skipRegCompleters="getIsTxtEditor"
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
 * Change Date: 2026-10-04
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapGetters, mapMutations, mapState } from 'vuex'
import SidebarCtr from './SidebarCtr.vue'
import DdlEditorCtr from './DdlEditorCtr.vue'
import TxtEditorCtr from './TxtEditorCtr.vue'
import SessionNavCtr from './SessionNavCtr.vue'
import ExecuteSqlDialog from './ExecuteSqlDialog.vue'
import FileDlgCtr from './FileDlgCtr.vue'

export default {
    name: 'wke-ctr',
    components: {
        SidebarCtr,
        TxtEditorCtr,
        DdlEditorCtr,
        SessionNavCtr,
        ExecuteSqlDialog,
        FileDlgCtr,
    },
    props: {
        ctrDim: { type: Object, required: true },
    },
    data() {
        return {
            sessTabCtrHeight: 30,
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
            is_sidebar_collapsed: state => state.queryPersisted.is_sidebar_collapsed,
            sidebar_pct: state => state.queryPersisted.sidebar_pct,
            curr_editor_mode: state => state.editor.curr_editor_mode,
            active_sql_conn: state => state.queryConn.active_sql_conn,
            query_sessions: state => state.querySession.query_sessions,
        }),
        ...mapGetters({
            getIsTxtEditor: 'editor/getIsTxtEditor',
            getExeStmtResultMap: 'schemaSidebar/getExeStmtResultMap',
            getActiveSessionId: 'querySession/getActiveSessionId',
            getDbCmplList: 'schemaSidebar/getDbCmplList',
        }),
        collapsedSidebarPct() {
            return this.$helpers.pxToPct({ px: 40, containerPx: this.ctrDim.width })
        },
        expandedMinSidebarPct() {
            return this.$helpers.pxToPct({ px: 120, containerPx: this.ctrDim.width })
        },
        minSidebarPct() {
            if (this.is_sidebar_collapsed) return this.collapsedSidebarPct
            return this.expandedMinSidebarPct
        },
        defSidebarPct() {
            return this.$helpers.pxToPct({ px: 240, containerPx: this.ctrDim.width })
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
        sidebarWidth() {
            return (this.ctrDim.width * this.sidebarPct) / 100
        },
        editorDim() {
            return {
                width: this.ctrDim.width - this.sidebarWidth,
                height: this.ctrDim.height - this.sessTabCtrHeight,
            }
        },
        sidebarPct() {
            if (this.is_sidebar_collapsed) return this.collapsedSidebarPct
            return this.sidebar_pct
        },
    },
    mounted() {
        this.$nextTick(() => this.handleSetDefSidebarPct())
    },
    activated() {
        this.watch_active_sql_conn()
    },
    deactivated() {
        this.$typy(this.unwatch_active_sql_conn).safeFunction()
    },
    methods: {
        ...mapActions({ handleInitialFetch: 'wke/handleInitialFetch' }),
        ...mapMutations({
            SET_SIDEBAR_PCT: 'queryPersisted/SET_SIDEBAR_PCT',
            SET_IS_SIDEBAR_COLLAPSED: 'queryPersisted/SET_IS_SIDEBAR_COLLAPSED',
        }),
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
        handleSetDefSidebarPct() {
            if (!this.sidebar_pct) this.SET_SIDEBAR_PCT(this.defSidebarPct)
        },
        setEditorDim() {
            const editor = this.$typy(this.$refs, 'editor[0]').safeObjectOrEmpty
            if (editor.$el) {
                const { width, height } = editor.$el.getBoundingClientRect()
                if (width !== 0 || height !== 0) this.editorDim = { width, height }
            }
        },
        handleAutoCollapse(pct) {
            if (this.$helpers.pctToPx({ pct, containerPx: this.ctrDim.width }) <= 50)
                this.SET_IS_SIDEBAR_COLLAPSED(true)
            else this.SET_IS_SIDEBAR_COLLAPSED(false)
        },
        onResizing(v) {
            if (v >= this.expandedMinSidebarPct) this.SET_SIDEBAR_PCT(v)
            this.handleAutoCollapse(v)
        },
    },
}
</script>
