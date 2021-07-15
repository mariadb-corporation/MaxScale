<template>
    <div v-resize.quiet="setPanelsPct" class="fill-height">
        <div
            ref="paneContainer"
            class="query-page d-flex flex-column fill-height"
            :class="{ 'query-page--fullscreen': isFullscreen }"
        >
            <toolbar-container
                ref="toolbarContainer"
                :queryTxt="queryTxt"
                :isFullscreen="isFullscreen"
                @is-fullscreen="isFullscreen = $event"
                @show-vis-sidebar="showVisSidebar = $event"
            />
            <split-pane
                v-if="minSidebarPct"
                v-model="sidebarPct"
                class="query-page__content"
                :minPercent="minSidebarPct"
                split="vert"
                :disable="isSidebarCollapsed"
            >
                <template slot="pane-left">
                    <sidebar-container
                        v-if="$typy($refs, 'worksheets.$refs.wke').isDefined"
                        :isSidebarCollapsed="isSidebarCollapsed"
                        @get-curr-prvw-data-schemaId="previewDataSchemaId = $event"
                        @is-sidebar-collapsed="isSidebarCollapsed = $event"
                        @place-to-editor="$refs.worksheets.$refs.wke[0].placeToEditor"
                        @dragging-schema="$refs.worksheets.$refs.wke[0].draggingSchema"
                        @drop-schema-to-editor="$refs.worksheets.$refs.wke[0].dropSchemaToEditor"
                    />
                </template>
                <template slot="pane-right">
                    <worksheets
                        ref="worksheets"
                        :containerHeight="containerHeight"
                        :previewDataSchemaId="previewDataSchemaId"
                        :showVisSidebar="showVisSidebar"
                        @query-txt="queryTxt = $event"
                        @onCtrlEnter="() => $refs.toolbarContainer.handleRun('all')"
                        @onCtrlShiftEnter="() => $refs.toolbarContainer.handleRun('selected')"
                    />
                </template>
            </split-pane>
        </div>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import SidebarContainer from './SidebarContainer'
import { mapActions, mapState, mapGetters } from 'vuex'
import ToolbarContainer from './ToolbarContainer'
import Worksheets from './Worksheets.vue'
export default {
    name: 'query-view',
    components: {
        SidebarContainer,
        ToolbarContainer,
        Worksheets,
    },
    data() {
        return {
            containerHeight: 0,
            minSidebarPct: 0,
            sidebarPct: 0,
            isFullscreen: false,
            isSidebarCollapsed: false,
            previewDataSchemaId: '',
            showVisSidebar: false,
            queryTxt: { all: '', selected: '' },
        }
    },
    computed: {
        ...mapState({
            checking_active_conn: state => state.query.checking_active_conn,
            curr_cnct_resource: state => state.query.curr_cnct_resource,
            active_conn_state: state => state.query.active_conn_state,
        }),
        ...mapGetters({
            getDbCmplList: 'query/getDbCmplList',
        }),
    },
    watch: {
        isFullscreen() {
            this.$nextTick(() => {
                // recalculate panes percent
                this.setPanelsPct()
            })
        },
        isSidebarCollapsed(v) {
            this.$nextTick(() => this.handleSetSidebarPct({ isSidebarCollapsed: v }))
        },
        sidebarPct() {
            this.$help.doubleRAF(() => {
                if (this.$typy(this.$refs, 'worksheets.$refs.wke').isDefined) {
                    this.$refs.worksheets.$refs.wke[0].setResultPaneDim()
                }
            })
        },
    },
    async created() {
        await this.checkActiveConn()
        if (this.active_conn_state) await this.updateActiveDb()
    },
    async beforeDestroy() {
        if (process.env.NODE_ENV !== 'development' && this.curr_cnct_resource)
            await this.disconnect()
    },
    mounted() {
        this.$help.doubleRAF(() => this.setPanelsPct())
    },
    methods: {
        ...mapActions({
            disconnect: 'query/disconnect',
            checkActiveConn: 'query/checkActiveConn',
            updateActiveDb: 'query/updateActiveDb',
        }),
        setPanelsPct() {
            this.handleSetSidebarPct({ isSidebarCollapsed: this.isSidebarCollapsed })
            /**
             * get pane container height then pass it via props to worksheets
             * to calculate min percent of worksheets child panes
             */
            this.containerHeight = this.$refs.paneContainer.clientHeight
        },
        handleSetSidebarPct({ isSidebarCollapsed }) {
            const containerWidth = this.$refs.paneContainer.clientWidth
            if (isSidebarCollapsed) {
                this.minSidebarPct = this.$help.pxToPct({ px: 40, containerPx: containerWidth })
                this.sidebarPct = this.minSidebarPct
            } else {
                this.minSidebarPct = this.$help.pxToPct({ px: 200, containerPx: containerWidth })
                this.sidebarPct = this.$help.pxToPct({ px: 240, containerPx: containerWidth })
            }
        },
    },
}
</script>

<style lang="scss" scoped>
$header-height: 50px;
.query-page {
    background: #ffffff;
    &--fullscreen {
        padding: 0px !important;
        width: 100%;
        height: calc(100% - #{$header-height});
        margin-left: -90px;
        margin-top: -24px;
        z-index: 7;
        position: fixed;
        overflow: hidden;
    }
}
</style>
