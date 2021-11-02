<template>
    <split-pane
        v-model="sidebarPct"
        class="query-page__content"
        :minPercent="minSidebarPct"
        split="vert"
        :disable="is_sidebar_collapsed"
    >
        <template slot="pane-left">
            <sidebar-container
                @place-to-editor="$typy($refs.txtEditorPane, 'placeToEditor').safeFunction($event)"
                @on-dragging="$typy($refs.txtEditorPane, 'draggingTxt').safeFunction($event)"
                @on-dragend="
                    $typy($refs.txtEditorPane, 'dropTxtToEditor').safeFunction({
                        e: $event,
                        type: 'schema',
                    })
                "
            />
        </template>
        <template slot="pane-right">
            <keep-alive>
                <txt-editor-container
                    v-if="isTxtEditor"
                    ref="txtEditorPane"
                    :dim="txtEditorPaneDim"
                    v-on="$listeners"
                />
                <ddl-editor-container v-else ref="ddlEditor" :dynDim="ddlDim" />
            </keep-alive>
        </template>
    </split-pane>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import SidebarContainer from './SidebarContainer'
import { mapGetters, mapState } from 'vuex'
import DDLEditorContainer from './DDLEditorContainer.vue'
import TxtEditorContainer from './TxtEditorContainer.vue'
export default {
    name: 'worksheet',
    components: {
        SidebarContainer,
        'txt-editor-container': TxtEditorContainer,
        'ddl-editor-container': DDLEditorContainer,
    },
    props: {
        ctrDim: { type: Object, required: true },
    },
    data() {
        return {
            // split-pane states
            txtEditorPaneDim: { width: 0, height: 0 },
            ddlDim: { height: 0, width: 0 },
            sidebarPct: 0,
        }
    },
    computed: {
        ...mapState({
            SQL_EDITOR_MODES: state => state.app_config.SQL_EDITOR_MODES,
            show_vis_sidebar: state => state.query.show_vis_sidebar,
            query_txt: state => state.query.query_txt,
            is_sidebar_collapsed: state => state.query.is_sidebar_collapsed,
        }),
        ...mapGetters({
            getDbCmplList: 'query/getDbCmplList',
            getCurrEditorMode: 'query/getCurrEditorMode',
        }),
        isTxtEditor() {
            return this.getCurrEditorMode === this.SQL_EDITOR_MODES.TXT_EDITOR
        },
        minSidebarPct() {
            if (!this.ctrDim.width) return 0
            if (this.is_sidebar_collapsed)
                return this.$help.pxToPct({ px: 40, containerPx: this.ctrDim.width })
            else return this.$help.pxToPct({ px: 200, containerPx: this.ctrDim.width })
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
        getCurrEditorMode() {
            this.$nextTick(() => this.handleRecalPanesDim())
        },
    },
    created() {
        this.$help.doubleRAF(() => {
            this.handleSetSidebarPct()
        })
    },
    activated() {
        this.addIsSidebarCollapsedsWatcher()
    },
    deactivated() {
        this.rmIsSidebarCollapsedsWatcher()
    },
    methods: {
        //Watchers to work with multiple worksheets which are kept alive
        addIsSidebarCollapsedsWatcher() {
            this.rmIsSidebarCollapsedsWatcher = this.$watch('is_sidebar_collapsed', () =>
                this.handleSetSidebarPct()
            )
        },
        // panes dimension/percentages calculation functions
        handleSetSidebarPct() {
            if (this.is_sidebar_collapsed) this.sidebarPct = this.minSidebarPct
            else this.sidebarPct = this.$help.pxToPct({ px: 240, containerPx: this.ctrDim.width })
        },
        setTxtEditorPaneDim() {
            if (this.$refs.txtEditorPane.$el) {
                const { width, height } = this.$refs.txtEditorPane.$el.getBoundingClientRect()
                if (width !== 0 || height !== 0) this.txtEditorPaneDim = { width, height }
            }
        },
        setDdlDim() {
            if (this.$refs.ddlEditor) {
                const { width, height } = this.$refs.ddlEditor.$el.getBoundingClientRect()
                if (width !== 0 || height !== 0) this.ddlDim = { width, height }
            }
        },
        handleRecalPanesDim() {
            switch (this.getCurrEditorMode) {
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
