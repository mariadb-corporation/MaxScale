<template>
    <v-progress-linear v-if="isInitializing" indeterminate />
    <mxs-split-pane
        v-else
        v-model="sidebarPct"
        class="query-view__content"
        :boundary="ctrDim.width"
        :minPercent="minSidebarPct"
        :deactivatedMinPctZone="deactivatedMinSizeBarPoint"
        :maxPercent="maxSidebarPct"
        split="vert"
        progress
        @resizing="onResizing"
    >
        <template slot="pane-left">
            <sidebar-ctr
                :queryEditorId="queryEditorId"
                :queryEditorTmp="queryEditorTmp"
                :activeQueryTabId="activeQueryTabId"
                :activeQueryTabConn="activeQueryTabConn"
                @place-to-editor="$typy($refs, 'editor[0].placeToEditor').safeFunction($event)"
                @on-dragging="$typy($refs, 'editor[0].draggingTxt').safeFunction($event)"
                @on-dragend="$typy($refs, 'editor[0].dropTxtToEditor').safeFunction($event)"
            />
        </template>
        <template slot="pane-right">
            <div class="d-flex flex-column fill-height">
                <query-tab-nav-ctr
                    :queryEditorId="queryEditorId"
                    :activeQueryTabId="activeQueryTabId"
                    :activeQueryTabConn="activeQueryTabConn"
                    :queryTabs="queryTabs"
                    :height="queryTabCtrHeight"
                >
                    <slot v-for="(_, slot) in $slots" :slot="slot" :name="slot" />
                </query-tab-nav-ctr>
                <keep-alive v-for="queryTab in queryTabs" :key="queryTab.id" :max="20">
                    <template v-if="activeQueryTabId === queryTab.id">
                        <txt-editor-ctr
                            v-if="isSqlEditor"
                            ref="editor"
                            :queryEditorTmp="queryEditorTmp"
                            :queryTab="queryTab"
                            :dim="editorDim"
                        />
                        <alter-table-editor
                            v-else-if="isAlterEditor"
                            :queryEditorTmp="queryEditorTmp"
                            :queryTab="queryTab"
                            :dim="editorDim"
                        />
                        <insight-viewer v-else :dim="editorDim" :queryTab="queryTab" />
                    </template>
                </keep-alive>
            </div>
        </template>
    </mxs-split-pane>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapMutations, mapState } from 'vuex'
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import QueryEditorTmp from '@wsModels/QueryEditorTmp'
import QueryTab from '@wsModels/QueryTab'
import SidebarCtr from '@wkeComps/QueryEditor/SidebarCtr.vue'
import InsightViewer from '@wkeComps/QueryEditor/InsightViewer.vue'
import AlterTableEditor from '@wkeComps/QueryEditor/AlterTableEditor.vue'
import TxtEditorCtr from '@wkeComps/QueryEditor/TxtEditorCtr.vue'
import QueryTabNavCtr from '@wkeComps/QueryEditor/QueryTabNavCtr.vue'

export default {
    name: 'query-editor',
    components: {
        SidebarCtr,
        InsightViewer,
        AlterTableEditor,
        TxtEditorCtr,
        QueryTabNavCtr,
    },
    props: {
        ctrDim: { type: Object, required: true },
        queryEditorId: { type: String, required: true },
    },
    data() {
        return {
            queryTabCtrHeight: 30,
            isInitializing: true,
        }
    },
    computed: {
        ...mapState({
            is_sidebar_collapsed: state => state.prefAndStorage.is_sidebar_collapsed,
            sidebar_pct_width: state => state.prefAndStorage.sidebar_pct_width,
            QUERY_TAB_TYPES: state => state.mxsWorkspace.config.QUERY_TAB_TYPES,
        }),
        queryEditor() {
            return QueryEditor.query().find(this.queryEditorId) || {}
        },
        queryEditorTmp() {
            return QueryEditorTmp.find(this.queryEditorId) || {}
        },
        queryTabs() {
            return (
                QueryTab.query()
                    .where(t => t.query_editor_id === this.queryEditorId)
                    .get() || []
            )
        },
        activeQueryTabId() {
            return this.$typy(this.queryEditor, 'active_query_tab_id').safeString
        },
        activeQueryTab() {
            return QueryTab.find(this.activeQueryTabId) || {}
        },
        activeQueryTabConn() {
            return QueryConn.getters('findQueryTabConn')(this.activeQueryTabId)
        },
        activeQueryTabConnId() {
            return this.$typy(this.activeQueryTabConn, 'id').safeString
        },
        isSqlEditor() {
            return this.activeQueryTab.type === this.QUERY_TAB_TYPES.SQL_EDITOR
        },
        isAlterEditor() {
            return this.activeQueryTab.type === this.QUERY_TAB_TYPES.ALTER_EDITOR
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
        activeQueryTabConnId: {
            deep: true,
            immediate: true,
            async handler(v, oV) {
                if (v !== oV) {
                    await QueryEditor.dispatch('handleInitialFetch')
                    this.isInitializing = false
                } else if (!v) this.isInitializing = false
            },
        },
    },
    mounted() {
        this.$nextTick(() => this.handleSetDefSidebarPct())
    },
    methods: {
        ...mapMutations({
            SET_SIDEBAR_PCT_WIDTH: 'prefAndStorage/SET_SIDEBAR_PCT_WIDTH',
            SET_IS_SIDEBAR_COLLAPSED: 'prefAndStorage/SET_IS_SIDEBAR_COLLAPSED',
        }),
        // panes dimension/percentages calculation functions
        handleSetDefSidebarPct() {
            if (!this.sidebarPct) this.sidebarPct = this.defSidebarPct
        },
        onResizing(v) {
            //auto collapse sidebar
            if (v <= this.minSidebarPct) this.SET_IS_SIDEBAR_COLLAPSED(true)
            else if (v >= this.deactivatedMinSizeBarPoint) this.SET_IS_SIDEBAR_COLLAPSED(false)
        },
    },
}
</script>
