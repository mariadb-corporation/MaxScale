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
                :selectedQueryTxt="selectedQueryTxt"
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
                        :isSidebarCollapsed="isSidebarCollapsed"
                        @get-curr-prvw-data-schemaId="previewDataSchemaId = $event"
                        @is-sidebar-collapsed="isSidebarCollapsed = $event"
                        @place-to-editor="placeToEditor"
                    />
                </template>
                <template slot="pane-right">
                    <!-- Main panel contains editor pane and visualize-sidebar pane -->
                    <split-pane
                        v-model="mainPanePct"
                        class="main-pane__content"
                        :minPercent="minMainPanePct"
                        split="vert"
                        disable
                    >
                        <!-- Editor pane contains editor and result pane -->
                        <template slot="pane-left">
                            <split-pane
                                ref="editorResultPane"
                                v-model="editorPct"
                                split="horiz"
                                :minPercent="minEditorPct"
                            >
                                <template slot="pane-left">
                                    <split-pane
                                        v-model="queryPanePct"
                                        class="editor__content"
                                        :minPercent="minQueryPanePct"
                                        split="vert"
                                        :disable="isChartMaximized || !showVisChart"
                                    >
                                        <!-- Editor pane contains editor and chart pane -->
                                        <template slot="pane-left">
                                            <query-editor
                                                ref="queryEditor"
                                                v-model="queryTxt"
                                                class="editor pt-2 pl-2"
                                                :cmplList="getDbCmplList"
                                                @on-selection="selectedQueryTxt = $event"
                                                @onCtrlEnter="
                                                    () => $refs.toolbarContainer.handleRun('all')
                                                "
                                                @onCtrlShiftEnter="
                                                    () =>
                                                        $refs.toolbarContainer.handleRun('selected')
                                                "
                                            />
                                        </template>
                                        <template slot="pane-right">
                                            <chart-container
                                                class="chart-pane"
                                                :selectedChart="selectedChart"
                                                :containerChartHeight="containerChartHeight"
                                                :chartData="chartData"
                                                :axisLabels="axisLabels"
                                                :xAxisType="xAxisType"
                                                :isChartMaximized="isChartMaximized"
                                                @is-chart-maximized="isChartMaximized = $event"
                                            />
                                        </template>
                                    </split-pane>
                                </template>
                                <template slot="pane-right">
                                    <query-result
                                        ref="queryResultPane"
                                        :dynDim="resultPaneDim"
                                        class="query-result"
                                        :previewDataSchemaId="previewDataSchemaId"
                                    />
                                </template>
                            </split-pane>
                        </template>
                        <template slot="pane-right">
                            <visualize-sidebar
                                class="visualize-sidebar"
                                @selected-chart="selectedChart = $event"
                                @get-chart-data="chartData = $event"
                                @get-axis-labels="axisLabels = $event"
                                @x-axis-type="xAxisType = $event"
                            />
                        </template>
                    </split-pane>
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
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import QueryEditor from '@/components/QueryEditor'
import SidebarContainer from './SidebarContainer'
import QueryResult from './QueryResult'
import { mapActions, mapState, mapGetters } from 'vuex'
import ToolbarContainer from './ToolbarContainer'
import VisualizeSideBar from './VisualizeSideBar'
import ChartContainer from './ChartContainer'
export default {
    name: 'query-view',
    components: {
        'query-editor': QueryEditor,
        SidebarContainer,
        QueryResult,
        ToolbarContainer,
        'visualize-sidebar': VisualizeSideBar,
        ChartContainer,
    },
    data() {
        return {
            minSidebarPct: 0,
            sidebarPct: 0,
            editorPct: 60,
            minEditorPct: 0,
            isFullscreen: false,
            isSidebarCollapsed: false,
            resultPaneDim: {
                height: 0,
                width: 0,
            },
            queryTxt: '',
            previewDataSchemaId: '',
            selectedQueryTxt: '',
            showVisSidebar: false,
            mainPanePct: 100,
            minMainPanePct: 0,
            queryPanePct: 100,
            minQueryPanePct: 0,
            // chart-container states
            selectedChart: '',
            chartData: {},
            axisLabels: { x: '', y: '' },
            xAxisType: '',
            isChartMaximized: false,
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
        showVisChart() {
            return this.selectedChart !== 'No Visualization'
        },
        containerChartHeight() {
            const { height: paneHeight } = this.resultPaneDim
            if (this.$refs.editorResultPane) {
                const { clientHeight } = this.$refs.editorResultPane.$el
                return clientHeight - paneHeight - 2 // minus border
            }
            return 0
        },
    },
    watch: {
        isFullscreen() {
            this.$nextTick(() => {
                this.handleSetSidebarPct({ isSidebarCollapsed: this.isSidebarCollapsed })
                this.handleSetMinEditorPct()
            })
        },
        isChartMaximized(v) {
            if (v) this.queryPanePct = this.minQueryPanePct
            else this.queryPanePct = 50
        },
        isSidebarCollapsed(v) {
            this.$nextTick(() => this.handleSetSidebarPct({ isSidebarCollapsed: v }))
        },
        sidebarPct() {
            this.$nextTick(() => this.setResultPaneDim())
        },
        editorPct() {
            this.$nextTick(() => this.setResultPaneDim())
        },
        showVisSidebar(v) {
            this.handleSetVisSidebar(v)
            this.$nextTick(() => this.setResultPaneDim())
        },
        selectedChart() {
            if (this.showVisChart) {
                this.queryPanePct = 50
                this.minQueryPanePct = this.pxToPct({
                    px: 32,
                    containerPx: this.resultPaneDim.width,
                })
            } else this.queryPanePct = 100
        },
    },
    async created() {
        await this.checkActiveConn()
        if (this.active_conn_state) await this.updateActiveDb()
        /*For development testing */
        if (process.env.NODE_ENV === 'development')
            this.queryTxt = 'SELECT * FROM test.randStr; SELECT * FROM mysql.help_topic'
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
            this.handleSetMinEditorPct()
        },
        setResultPaneDim() {
            if (this.$refs.queryResultPane) {
                const { clientWidth, clientHeight } = this.$refs.queryResultPane.$el
                this.resultPaneDim = {
                    width: clientWidth,
                    height: clientHeight,
                }
            }
        },
        handleSetMinEditorPct() {
            const containerHeight = this.$refs.paneContainer.clientHeight
            this.minEditorPct = this.pxToPct({ px: 26, containerPx: containerHeight })
        },
        handleSetSidebarPct({ isSidebarCollapsed }) {
            const containerWidth = this.$refs.paneContainer.clientWidth
            if (isSidebarCollapsed) {
                this.minSidebarPct = this.pxToPct({ px: 40, containerPx: containerWidth })
                this.sidebarPct = this.minSidebarPct
            } else {
                this.minSidebarPct = this.pxToPct({ px: 200, containerPx: containerWidth })
                this.sidebarPct = this.pxToPct({ px: 240, containerPx: containerWidth })
            }
        },
        handleSetVisSidebar(showVisSidebar) {
            if (showVisSidebar) {
                const visSidebarPct = this.pxToPct({
                    px: 250,
                    containerPx: this.resultPaneDim.width,
                })
                this.mainPanePct = 100 - visSidebarPct
            } else this.mainPanePct = 100
        },
        pxToPct: ({ px, containerPx }) => (px / containerPx) * 100,
        placeToEditor(schemaId) {
            this.$refs.queryEditor.insertAtCursor(schemaId)
        },
    },
}
</script>

<style lang="scss" scoped>
.editor,
.visualize-sidebar,
.query-result,
.chart-pane {
    border: 1px solid $table-border;
    width: 100%;
    height: 100%;
}
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
