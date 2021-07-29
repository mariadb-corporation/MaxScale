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
                @dragging-schema="draggingSchema"
                @drop-schema-to-editor="dropSchemaToEditor"
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
                                        @on-selection="
                                            SET_QUERY_TXT({
                                                ...query_txt,
                                                selected: $event,
                                            })
                                        "
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
 * Change Date: 2025-07-14
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
        allQueryTxt: {
            get() {
                return this.query_txt.all
            },
            set(value) {
                this.SET_QUERY_TXT({ ...this.query_txt, all: value })
            },
        },
    },
    watch: {
        sidebarPct() {
            this.$help.doubleRAF(() => {
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
        selectedChart() {
            if (this.showVisChart) {
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
    async created() {
        /*For development testing */
        if (process.env.NODE_ENV === 'development')
            this.SET_QUERY_TXT({
                ...this.query_txt,
                all: 'SELECT * FROM test.randStr; SELECT * FROM mysql.help_topic',
            })
    },
    activated() {
        this.$help.doubleRAF(() => {
            this.handleSetSidebarPct()
            this.handleSetMinEditorPct()
            this.setResultPaneDim()
            this.addShowVisSidebarWatcher()
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
                    containerPx: this.resultPaneDim.width,
                })
                this.mainPanePct = 100 - visSidebarPct
            } else this.mainPanePct = 100
        },
        // editor related functions
        placeToEditor(schemaId) {
            this.$refs.queryEditor.insertAtCursor({ text: schemaId })
        },
        draggingSchema(e) {
            const { editor, monaco } = this.$refs.queryEditor
            // build mouseDropWidget
            const preference = monaco.editor.ContentWidgetPositionPreference.EXACT
            const dropTarget = editor.getTargetAtClientPoint(e.clientX, e.clientY)
            if (dropTarget) {
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
                editor.addContentWidget(this.mouseDropWidget)
            } else if (this.mouseDropDOM) editor.removeContentWidget(this.mouseDropWidget)
        },
        dropSchemaToEditor({ e, name }) {
            if (name) {
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
                    insertAtCursor({
                        text: name,
                        range,
                    })
                    editor.removeContentWidget(this.mouseDropWidget)
                }
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
