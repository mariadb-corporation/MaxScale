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
                @place-to-editor="placeToEditor"
                @on-dragging="draggingTxt"
                @on-dragend="dropTxtToEditor({ e: $event, type: 'schema' })"
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
                                        v-model="allQueryTxt"
                                        class="editor pt-2 pl-2"
                                        :cmplList="getDbCmplList"
                                        isKeptAlive
                                        @on-selection="SET_SELECTED_QUERY_TXT($event)"
                                        v-on="$listeners"
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
                                @place-sql-in-editor="placeToEditor"
                                @on-dragging="draggingTxt"
                                @on-dragend="dropTxtToEditor({ e: $event, type: 'sql' })"
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
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import SidebarContainer from './SidebarContainer'
import QueryEditor from '@/components/QueryEditor'
import QueryResult from './QueryResult'
import { mapGetters, mapMutations, mapState } from 'vuex'
import VisualizeSideBar from './VisualizeSideBar'
import ChartContainer from './ChartContainer'
export default {
    name: 'worksheet',
    components: {
        SidebarContainer,
        'query-editor': QueryEditor,
        QueryResult,
        'visualize-sidebar': VisualizeSideBar,
        ChartContainer,
    },
    props: {
        ctrDim: { type: Object, required: true },
    },
    data() {
        return {
            // split-pane states
            minSidebarPct: 0,
            sidebarPct: 0,
            mainPanePct: 100,
            minMainPanePct: 0,
            editorPct: 60,
            minEditorPct: 0,
            queryPanePct: 100,
            minQueryPanePct: 0,
            mouseDropDOM: null, // mouse drop DOM node
            mouseDropWidget: null, // mouse drop widget while dragging to editor
            // chart-container states
            selectedChart: '',
            chartData: {},
            axisLabels: { x: '', y: '' },
            xAxisType: '',
            isChartMaximized: false,
            // query-result state
            resultPaneDim: {
                height: 0,
                width: 0,
            },
        }
    },
    computed: {
        ...mapState({
            show_vis_sidebar: state => state.query.show_vis_sidebar,
            query_txt: state => state.query.query_txt,
            is_sidebar_collapsed: state => state.query.is_sidebar_collapsed,
        }),
        ...mapGetters({
            getDbCmplList: 'query/getDbCmplList',
        }),
        showVisChart() {
            const datasets = this.$typy(this.chartData, 'datasets').safeArray
            return this.selectedChart !== 'No Visualization' && datasets.length
        },
        containerChartHeight() {
            const { height: paneHeight } = this.resultPaneDim
            if (this.$refs.editorResultPane) {
                const { clientHeight } = this.$refs.editorResultPane.$el
                return clientHeight - paneHeight - 2 // minus border
            }
            return 0
        },
        allQueryTxt: {
            get() {
                return this.query_txt
            },
            set(value) {
                this.SET_QUERY_TXT(value)
            },
        },
    },
    watch: {
        sidebarPct() {
            this.$nextTick(() => {
                this.setResultPaneDim()
            })
        },
        isChartMaximized(v) {
            if (v) this.queryPanePct = this.minQueryPanePct
            else this.queryPanePct = 50
        },
        editorPct() {
            this.$nextTick(() => this.setResultPaneDim())
        },
        showVisChart(v) {
            if (v) {
                this.queryPanePct = 50
                this.minQueryPanePct = this.$help.pxToPct({
                    px: 32,
                    containerPx: this.resultPaneDim.width,
                })
            } else this.queryPanePct = 100
        },
        'ctrDim.height'(v, oV) {
            if (oV) this.handleSetMinEditorPct()
        },
        'ctrDim.width'(v, oV) {
            if (oV) this.handleSetSidebarPct()
        },
        is_sidebar_collapsed() {
            this.handleSetSidebarPct()
        },
    },
    activated() {
        this.$help.doubleRAF(() => {
            this.handleSetSidebarPct()
            this.handleSetMinEditorPct()
            this.setResultPaneDim()
            this.addShowVisSidebarWatcher()
            this.$nextTick(() => this.handleSetVisSidebar(this.show_vis_sidebar))
        })
    },
    deactivated() {
        this.$help.doubleRAF(() => {
            this.unwatchShowVisSidebar()
        })
    },
    methods: {
        ...mapMutations({
            SET_QUERY_TXT: 'query/SET_QUERY_TXT',
            SET_SELECTED_QUERY_TXT: 'query/SET_SELECTED_QUERY_TXT',
        }),
        addShowVisSidebarWatcher() {
            this.unwatchShowVisSidebar = this.$watch('show_vis_sidebar', v => {
                this.handleSetVisSidebar(v)
                this.$nextTick(() => this.setResultPaneDim())
            })
        },
        // panes dimension/percentages calculation functions
        handleSetSidebarPct() {
            const containerWidth = this.ctrDim.width
            if (this.is_sidebar_collapsed) {
                this.minSidebarPct = this.$help.pxToPct({ px: 40, containerPx: containerWidth })
                this.sidebarPct = this.minSidebarPct
            } else {
                this.minSidebarPct = this.$help.pxToPct({ px: 200, containerPx: containerWidth })
                this.sidebarPct = this.$help.pxToPct({ px: 240, containerPx: containerWidth })
            }
        },
        handleSetMinEditorPct() {
            this.minEditorPct = this.$help.pxToPct({ px: 26, containerPx: this.ctrDim.height })
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
        handleSetVisSidebar(showVisSidebar) {
            if (showVisSidebar) {
                const visSidebarPct = this.$help.pxToPct({
                    px: 250,
                    containerPx: this.$refs.queryResultPane.$el.clientWidth,
                })
                this.mainPanePct = 100 - visSidebarPct
            } else this.mainPanePct = 100
        },
        // editor related functions
        placeToEditor(text) {
            this.$refs.queryEditor.insertAtCursor({ text })
        },
        handleGenMouseDropWidget(dropTarget) {
            /**
             *  Setting text cusor to all elements as a fallback method for firefox
             *  as monaco editor will fail to get dropTarget position in firefox
             *  So only add mouseDropWidget when user agent is not firefox
             */
            if (navigator.userAgent.includes('Firefox')) {
                if (dropTarget) document.body.className = 'cursor--all-text'
                else document.body.className = 'cursor--all-grabbing'
            } else {
                const { editor, monaco } = this.$refs.queryEditor
                document.body.className = 'cursor--all-grabbing'
                if (dropTarget) {
                    const preference = monaco.editor.ContentWidgetPositionPreference.EXACT
                    if (!this.mouseDropDOM) {
                        this.mouseDropDOM = document.createElement('div')
                        this.mouseDropDOM.style.pointerEvents = 'none'
                        this.mouseDropDOM.style.borderLeft = '2px solid #424f62'
                        this.mouseDropDOM.innerHTML = '&nbsp;'
                    }
                    this.mouseDropWidget = {
                        mouseDropDOM: null,
                        getId: () => 'drag',
                        getDomNode: () => this.mouseDropDOM,
                        getPosition: () => ({
                            position: dropTarget.position,
                            preference: [preference, preference],
                        }),
                    }
                    //remove the prev cusor widget first then add
                    editor.removeContentWidget(this.mouseDropWidget)
                    editor.addContentWidget(this.mouseDropWidget)
                } else if (this.mouseDropWidget) editor.removeContentWidget(this.mouseDropWidget)
            }
        },
        draggingTxt(e) {
            const { editor } = this.$refs.queryEditor
            // build mouseDropWidget
            const dropTarget = editor.getTargetAtClientPoint(e.clientX, e.clientY)
            this.handleGenMouseDropWidget(dropTarget)
        },
        dropTxtToEditor({ e, type }) {
            if (e.target.textContent) {
                const { editor, monaco, insertAtCursor } = this.$refs.queryEditor
                const dropTarget = editor.getTargetAtClientPoint(e.clientX, e.clientY)

                if (dropTarget) {
                    const dropPos = dropTarget.position
                    // create range
                    const range = new monaco.Range(
                        dropPos.lineNumber,
                        dropPos.column,
                        dropPos.lineNumber,
                        dropPos.column
                    )
                    let text = e.target.textContent.trim()
                    switch (type) {
                        case 'schema':
                            text = this.$help.escapeIdentifiers(text)
                            break
                    }
                    insertAtCursor({ text, range })
                    if (this.mouseDropWidget) editor.removeContentWidget(this.mouseDropWidget)
                }
                document.body.className = ''
            }
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
.editor {
    border-top: none;
}
</style>
