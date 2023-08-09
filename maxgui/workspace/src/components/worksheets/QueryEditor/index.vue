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
                    @place-to-editor="$typy($refs, 'editor[0].placeToEditor').safeFunction($event)"
                    @on-dragging="$typy($refs, 'editor[0].draggingTxt').safeFunction($event)"
                    @on-dragend="$typy($refs, 'editor[0].dropTxtToEditor').safeFunction($event)"
                />
            </template>
            <template slot="pane-right">
                <div class="d-flex flex-column fill-height">
                    <query-tab-nav-ctr :height="queryTabCtrHeight">
                        <slot v-for="(_, slot) in $slots" :slot="slot" :name="slot" />
                    </query-tab-nav-ctr>
                    <keep-alive v-for="queryTab in allQueryTabs" :key="queryTab.id" :max="20">
                        <template v-if="activeQueryTabId === queryTab.id">
                            <txt-editor-ctr
                                v-if="isTxtEditor"
                                ref="editor"
                                :queryTab="queryTab"
                                :dim="editorDim"
                            />
                            <alter-table-editor v-else :dim="editorDim" />
                        </template>
                    </keep-alive>
                </div>
            </template>
        </mxs-split-pane>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-07-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapMutations, mapState } from 'vuex'
import QueryConn from '@wsModels/QueryConn'
import QueryEditor from '@wsModels/QueryEditor'
import QueryTab from '@wsModels/QueryTab'
import AlterTableEditor from '@wkeComps/QueryEditor/AlterTableEditor.vue'
import SidebarCtr from '@wkeComps/QueryEditor/SidebarCtr.vue'
import TxtEditorCtr from '@wkeComps/QueryEditor/TxtEditorCtr.vue'
import QueryTabNavCtr from '@wkeComps/QueryEditor/QueryTabNavCtr.vue'

export default {
    name: 'query-editor',
    components: {
        AlterTableEditor,
        SidebarCtr,
        TxtEditorCtr,
        QueryTabNavCtr,
    },
    props: {
        ctrDim: { type: Object, required: true },
    },
    data() {
        return {
            queryTabCtrHeight: 30,
        }
    },
    computed: {
        ...mapState({
            is_sidebar_collapsed: state => state.prefAndStorage.is_sidebar_collapsed,
            sidebar_pct_width: state => state.prefAndStorage.sidebar_pct_width,
        }),
        activeQueryTabConn() {
            return QueryConn.getters('activeQueryTabConn')
        },
        isTxtEditor() {
            return QueryTab.getters('isTxtEditor')
        },
        allQueryTabs() {
            return QueryTab.all()
        },
        activeQueryTabId() {
            return QueryEditor.getters('activeQueryTabId')
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
    },
    mounted() {
        this.$nextTick(() => this.handleSetDefSidebarPct())
    },
    activated() {
        this.watch_activeQueryTabConn()
    },
    deactivated() {
        this.$typy(this.unwatch_activeQueryTabConn).safeFunction()
    },
    methods: {
        ...mapMutations({
            SET_SIDEBAR_PCT_WIDTH: 'prefAndStorage/SET_SIDEBAR_PCT_WIDTH',
            SET_IS_SIDEBAR_COLLAPSED: 'prefAndStorage/SET_IS_SIDEBAR_COLLAPSED',
        }),
        /**
         * A watcher on activeQueryTabConn state that is triggered immediately
         * to behave like a created hook. The watcher is watched/unwatched based on
         * activated/deactivated hook to prevent it from being triggered while changing
         * the value of activeQueryTabConn in another worksheet.
         */
        watch_activeQueryTabConn() {
            this.unwatch_activeQueryTabConn = this.$watch(
                'activeQueryTabConn.id',
                async (v, oV) => {
                    if (v !== oV) await QueryEditor.dispatch('handleInitialFetch')
                },
                { deep: true, immediate: true }
            )
        },
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
