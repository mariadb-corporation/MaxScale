<template>
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
                                v-model="queryTxt.all"
                                class="editor pt-2 pl-2"
                                :cmplList="getDbCmplList"
                                @on-selection="queryTxt.selected = $event"
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
import QueryEditor from '@/components/QueryEditor'
import QueryResult from './QueryResult'
import { mapGetters } from 'vuex'
import VisualizeSideBar from './VisualizeSideBar'
import ChartContainer from './ChartContainer'
export default {
    name: 'worksheet',
    components: {
        'query-editor': QueryEditor,
        QueryResult,
        'visualize-sidebar': VisualizeSideBar,
        ChartContainer,
    },
    props: {
        containerHeight: { type: Number, required: true },
        previewDataSchemaId: { type: String, required: true },
        showVisSidebar: { type: Boolean, required: true },
    },
    data() {
        return {
            // split-pane states
            mainPanePct: 100,
            minMainPanePct: 0,
            editorPct: 60,
            minEditorPct: 0,
            queryPanePct: 100,
            minQueryPanePct: 0,
            // query-editor state
            queryTxt: {
                all: '',
                selected: '',
            },
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
        isChartMaximized(v) {
            if (v) this.queryPanePct = this.minQueryPanePct
            else this.queryPanePct = 50
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
                this.minQueryPanePct = this.$help.pxToPct({
                    px: 32,
                    containerPx: this.resultPaneDim.width,
                })
            } else this.queryPanePct = 100
        },
        containerHeight(v) {
            this.minEditorPct = this.$help.pxToPct({ px: 26, containerPx: v })
        },
        queryTxt: {
            deep: true,
            handler(v) {
                this.$emit('query-txt', v)
            },
        },
    },
    async created() {
        /*For development testing */
        if (process.env.NODE_ENV === 'development')
            this.queryTxt.all = 'SELECT * FROM test.randStr; SELECT * FROM mysql.help_topic'
    },
    methods: {
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
</style>
