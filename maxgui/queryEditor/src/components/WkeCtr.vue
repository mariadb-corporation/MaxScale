<template>
    <div class="fill-height">
        <mxs-split-pane
            v-model="sidebarPct"
            class="query-view__content"
            :boundary="ctrDim.width"
            :minPercent="minSidebarPct"
            :deactivatedMinPctZone="deactivatedMinSizeBarPoint"
            :maxPercent="maxSidebarPct"
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
                    <query-tab-nav-ctr :height="queryTabCtrHeight" />
                    <keep-alive v-for="queryTab in allQueryTabs" :key="queryTab.id" :max="20">
                        <template v-if="activeQueryTabId === queryTab.id">
                            <txt-editor-ctr
                                v-if="isTxtEditor"
                                ref="editor"
                                :queryTab="queryTab"
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
            :skipRegCompleters="isTxtEditor"
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
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapGetters, mapMutations, mapState } from 'vuex'
import Worksheet from '@queryEditorSrc/store/orm/models/Worksheet'
import QueryTab from '@queryEditorSrc/store/orm/models/QueryTab'
import Editor from '@queryEditorSrc/store/orm/models/Editor'
import SidebarCtr from './SidebarCtr.vue'
import DdlEditorCtr from './DdlEditorCtr.vue'
import TxtEditorCtr from './TxtEditorCtr.vue'
import QueryTabNavCtr from './QueryTabNavCtr.vue'
import ExecuteSqlDialog from './ExecuteSqlDialog.vue'
import FileDlgCtr from './FileDlgCtr.vue'

export default {
    name: 'wke-ctr',
    components: {
        SidebarCtr,
        TxtEditorCtr,
        DdlEditorCtr,
        QueryTabNavCtr,
        ExecuteSqlDialog,
        FileDlgCtr,
    },
    props: {
        ctrDim: { type: Object, required: true },
    },
    data() {
        return {
            queryTabCtrHeight: 30,
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
            sidebar_pct_width: state => state.queryPersisted.sidebar_pct_width,
        }),
        ...mapGetters({
            getActiveQueryTabConn: 'queryConns/getActiveQueryTabConn',
            getExeStmtResultMap: 'schemaSidebar/getExeStmtResultMap',
            getDbCmplList: 'schemaSidebar/getDbCmplList',
        }),
        isTxtEditor() {
            return Editor.getters('getIsTxtEditor')
        },
        allQueryTabs() {
            return QueryTab.all()
        },
        activeQueryTabId() {
            return QueryTab.getters('getActiveQueryTabId')
        },
        minSidebarPct() {
            return this.$helpers.pxToPct({ px: 40, containerPx: this.ctrDim.width })
        },
        deactivatedMinSizeBarPoint() {
            return this.minSidebarPct * 3
        },
        maxSidebarPct() {
            return 100 - this.$helpers.pxToPct({ px: 370, containerPx: this.ctrDim.width })
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
                height: this.ctrDim.height - this.queryTabCtrHeight,
            }
        },
        sidebarPct: {
            get() {
                if (this.is_sidebar_collapsed) return this.minSidebarPct
                return this.sidebar_pct_width
            },
            set(v) {
                this.SET_SIDEBAR_PCT_WIDTH(v)
            },
        },
    },
    watch: {
        /**
         * When sidebar is expanded by clicking the expand button,
         * if the current sidebar percent width <= the minimum sidebar percent
         * assign the default percent
         */
        is_sidebar_collapsed(v) {
            if (!v && this.sidebarPct <= this.minSidebarPct) this.sidebarPct = this.defSidebarPct
        },
    },
    mounted() {
        this.$nextTick(() => this.handleSetDefSidebarPct())
    },
    activated() {
        this.watch_getActiveQueryTabConn()
    },
    deactivated() {
        this.$typy(this.unwatch_getActiveQueryTabConn).safeFunction()
    },
    methods: {
        ...mapMutations({
            SET_SIDEBAR_PCT_WIDTH: 'queryPersisted/SET_SIDEBAR_PCT_WIDTH',
            SET_IS_SIDEBAR_COLLAPSED: 'queryPersisted/SET_IS_SIDEBAR_COLLAPSED',
        }),
        /**
         * A watcher on getActiveQueryTabConn state that is triggered immediately
         * to behave like a created hook. The watcher is watched/unwatched based on
         * activated/deactivated hook to prevent it from being triggered while changing
         * the value of getActiveQueryTabConn in another worksheet.
         */
        watch_getActiveQueryTabConn() {
            this.unwatch_getActiveQueryTabConn = this.$watch(
                'getActiveQueryTabConn',
                async () => await Worksheet.dispatch('handleInitialFetch'),
                { deep: true, immediate: true }
            )
        },
        // panes dimension/percentages calculation functions
        handleSetDefSidebarPct() {
            if (!this.sidebarPct) this.sidebarPct = this.defSidebarPct
        },
        setEditorDim() {
            const editor = this.$typy(this.$refs, 'editor[0]').safeObjectOrEmpty
            if (editor.$el) {
                const { width, height } = editor.$el.getBoundingClientRect()
                if (width !== 0 || height !== 0) this.editorDim = { width, height }
            }
        },
        onResizing(v) {
            //auto collapse sidebar
            if (v <= this.minSidebarPct) this.SET_IS_SIDEBAR_COLLAPSED(true)
            else if (v >= this.deactivatedMinSizeBarPoint) this.SET_IS_SIDEBAR_COLLAPSED(false)
        },
    },
}
</script>
