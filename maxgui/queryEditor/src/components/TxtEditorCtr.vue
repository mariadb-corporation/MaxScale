<template>
    <div class="d-flex flex-column fill-height">
        <!-- ref is needed here so that its parent can call method in it  -->
        <txt-editor-toolbar-ctr
            class="d-flex"
            :height="txtEditorToolbarHeight"
            :session="session"
            @disable-tab-move-focus="toggleTabMoveFocus"
        >
            <slot v-for="(_, slot) in $slots" :slot="slot" :name="slot" />
        </txt-editor-toolbar-ctr>
        <!-- Main panel contains editor pane and chart-config -->
        <mxs-split-pane
            v-model="mainPanePct"
            class="main-pane__content d-flex"
            :minPercent="minMainPanePct"
            split="vert"
            disable
        >
            <template slot="pane-left">
                <!-- Editor pane contains editor and result pane -->
                <mxs-split-pane
                    ref="editorResultPane"
                    v-model="editorPct"
                    split="horiz"
                    :minPercent="minEditorPct"
                >
                    <template slot="pane-left">
                        <mxs-split-pane
                            v-model="queryPanePct"
                            class="editor__content"
                            :minPercent="minQueryPanePct"
                            split="vert"
                            :disable="isChartMaximized || !showVisChart"
                        >
                            <template slot="pane-left">
                                <sql-editor
                                    ref="sqlEditor"
                                    v-model="allQueryTxt"
                                    class="editor pt-2 pl-2"
                                    :cmplList="cmplList"
                                    isKeptAlive
                                    :isTabMoveFocus.sync="isTabMoveFocus"
                                    @on-selection="SET_SELECTED_QUERY_TXT($event)"
                                    @shortkey="eventBus.$emit('shortkey', $event)"
                                />
                            </template>
                            <template slot="pane-right">
                                <chart-pane
                                    v-if="!$typy(chartOpt, 'data.datasets').isEmptyArray"
                                    v-model="chartOpt"
                                    :containerHeight="chartContainerHeight"
                                    :chartTypes="SQL_CHART_TYPES"
                                    :axisTypes="SQL_CHART_AXIS_TYPES"
                                    class="chart-pane"
                                    @close-chart="setDefChartOptState"
                                />
                            </template>
                        </mxs-split-pane>
                    </template>
                    <template slot="pane-right">
                        <query-result-ctr
                            ref="queryResultPane"
                            :dynDim="resultPaneDim"
                            class="query-result-ctr"
                            @place-to-editor="placeToEditor"
                            @on-dragging="draggingTxt"
                            @on-dragend="dropTxtToEditor"
                        />
                    </template>
                </mxs-split-pane>
            </template>
            <template slot="pane-right">
                <chart-config
                    v-if="show_vis_sidebar"
                    v-model="chartOpt"
                    :chartTypes="SQL_CHART_TYPES"
                    :axisTypes="SQL_CHART_AXIS_TYPES"
                    :sqlQueryModes="SQL_QUERY_MODES"
                    :resultSets="resultSets"
                    class="chart-config"
                />
            </template>
        </mxs-split-pane>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapGetters, mapMutations, mapState } from 'vuex'
import TxtEditorToolbarCtr from './TxtEditorToolbarCtr.vue'
import SqlEditor from './SqlEditor'
import QueryResultCtr from './QueryResultCtr.vue'
import ChartConfig from './ChartConfig'
import ChartPane from './ChartPane'
import { EventBus } from './EventBus'

export default {
    name: 'txt-editor-ctr',
    components: {
        TxtEditorToolbarCtr,
        'sql-editor': SqlEditor,
        QueryResultCtr,
        ChartConfig,
        ChartPane,
    },
    props: {
        dim: { type: Object, required: true },
        session: { type: Object, required: true },
    },
    data() {
        return {
            // mxs-split-pane states
            mainPanePct: 100,
            minMainPanePct: 0,
            editorPct: 60,
            minEditorPct: 0,
            queryPanePct: 100,
            minQueryPanePct: 0,
            mouseDropDOM: null, // mouse drop DOM node
            mouseDropWidget: null, // mouse drop widget while dragging to editor
            maxVisSidebarPx: 250,
            txtEditorToolbarHeight: 28,
            // chart-config and chart-pane state
            defChartOpt: {
                type: '',
                data: { datasets: [] },
                scaleLabels: { x: '', y: '' },
                axesType: { x: '', y: '' },
                isMaximized: false,
            },
            chartOpt: null,
        }
    },
    computed: {
        ...mapState({
            show_vis_sidebar: state => state.queryResult.show_vis_sidebar,
            query_txt: state => state.editor.query_txt,
            is_sidebar_collapsed: state => state.schemaSidebar.is_sidebar_collapsed,
            query_snippets: state => state.queryPersisted.query_snippets,
            CMPL_SNIPPET_KIND: state => state.queryEditorConfig.config.CMPL_SNIPPET_KIND,
            SQL_CHART_TYPES: state => state.queryEditorConfig.config.SQL_CHART_TYPES,
            SQL_CHART_AXIS_TYPES: state => state.queryEditorConfig.config.SQL_CHART_AXIS_TYPES,
            SQL_QUERY_MODES: state => state.queryEditorConfig.config.SQL_QUERY_MODES,
            tab_moves_focus: state => state.queryPersisted.tab_moves_focus,
        }),
        ...mapGetters({
            getDbCmplList: 'schemaSidebar/getDbCmplList',
            getActiveSessionId: 'querySession/getActiveSessionId',
            getChartResultSets: 'queryResult/getChartResultSets',
        }),
        eventBus() {
            return EventBus
        },
        isTabMoveFocus: {
            get() {
                return this.tab_moves_focus
            },
            set(v) {
                this.SET_TAB_MOVES_FOCUS(v)
            },
        },
        resultSets() {
            return this.getChartResultSets({ scope: this })
        },
        snippetList() {
            return this.query_snippets.map(q => ({
                label: q.name,
                detail: `SNIPPET - ${q.sql}`,
                insertText: q.sql,
                type: this.CMPL_SNIPPET_KIND,
            }))
        },
        cmplList() {
            return [...this.getDbCmplList, ...this.snippetList]
        },
        showVisChart() {
            const datasets = this.$typy(this.chartOpt, 'data.datasets').safeArray
            return Boolean(this.$typy(this.chartOpt, 'type').safeString && datasets.length)
        },
        isChartMaximized() {
            return this.$typy(this.chartOpt, 'isMaximized').safeBoolean
        },
        panesDim() {
            return { width: this.dim.width, height: this.dim.height - this.txtEditorToolbarHeight }
        },
        chartContainerHeight() {
            return (this.panesDim.height * this.editorPct) / 100
        },
        maxVisSidebarPct() {
            return this.$helpers.pxToPct({
                px: this.maxVisSidebarPx,
                containerPx: this.panesDim.width,
            })
        },
        allQueryTxt: {
            get() {
                return this.query_txt
            },
            set(value) {
                this.SET_QUERY_TXT({ payload: value, id: this.getActiveSessionId })
            },
        },
        resultPaneDim() {
            const visSideBarWidth = this.show_vis_sidebar ? this.maxVisSidebarPx : 0
            return {
                width: this.panesDim.width - visSideBarWidth,
                height: (this.panesDim.height * (100 - this.editorPct)) / 100,
            }
        },
    },
    watch: {
        isChartMaximized(v) {
            if (v) this.queryPanePct = this.minQueryPanePct
            else this.queryPanePct = 50
        },
        showVisChart(v) {
            if (v) {
                this.queryPanePct = 50
                this.minQueryPanePct = this.$helpers.pxToPct({
                    px: 32,
                    containerPx: this.panesDim.width,
                })
            } else this.queryPanePct = 100
        },
        'panesDim.height'(v) {
            if (v) this.handleSetMinEditorPct()
        },
        'panesDim.width'() {
            this.handleSetVisSidebar(this.show_vis_sidebar)
        },
        show_vis_sidebar(v) {
            this.handleSetVisSidebar(v)
        },
    },
    created() {
        this.setDefChartOptState()
    },
    methods: {
        ...mapMutations({
            SET_QUERY_TXT: 'editor/SET_QUERY_TXT',
            SET_SELECTED_QUERY_TXT: 'editor/SET_SELECTED_QUERY_TXT',
            SET_TAB_MOVES_FOCUS: 'queryPersisted/SET_TAB_MOVES_FOCUS',
        }),
        toggleTabMoveFocus() {
            if (!this.$typy(this.$refs, 'sqlEditor.editor').isEmptyObject)
                this.$refs.sqlEditor.editor.trigger('', 'editor.action.toggleTabFocusMode')
        },
        setDefChartOptState() {
            this.chartOpt = this.$helpers.lodash.cloneDeep(this.defChartOpt)
        },
        handleSetMinEditorPct() {
            this.minEditorPct = this.$helpers.pxToPct({
                px: 26,
                containerPx: this.panesDim.height,
            })
        },
        handleSetVisSidebar(showVisSidebar) {
            if (!showVisSidebar) this.mainPanePct = 100
            else this.mainPanePct = 100 - this.maxVisSidebarPct
        },
        // editor related functions
        placeToEditor(text) {
            this.$refs.sqlEditor.insertAtCursor({ text })
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
                const { editor, monaco } = this.$refs.sqlEditor
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
            const { editor } = this.$refs.sqlEditor
            // build mouseDropWidget
            const dropTarget = editor.getTargetAtClientPoint(e.clientX, e.clientY)
            this.handleGenMouseDropWidget(dropTarget)
        },
        dropTxtToEditor(e) {
            if (e.target.textContent) {
                const { editor, monaco, insertAtCursor } = this.$refs.sqlEditor
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
.chart-config,
.query-result-ctr,
.chart-pane {
    width: 100%;
    height: 100%;
}
.chart-config,
.chart-pane {
    border-left: 1px solid $table-border;
}
</style>
