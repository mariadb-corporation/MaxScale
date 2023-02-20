<template>
    <div class="d-flex flex-column fill-height">
        <!-- ref is needed here so that its parent can call method in it  -->
        <txt-editor-toolbar-ctr
            class="d-flex"
            :height="txtEditorToolbarHeight"
            :queryTab="queryTab"
            @disable-tab-move-focus="toggleTabMoveFocus"
        />
        <!-- Main panel contains editor pane and chart-config -->
        <mxs-split-pane
            :value="mainPanePct"
            :boundary="panesDim.width"
            class="main-pane__content d-flex"
            split="vert"
            disable
        >
            <template slot="pane-left">
                <!-- Editor pane contains editor and result pane -->
                <mxs-split-pane
                    ref="editorResultPane"
                    v-model="queryPanePctHeight"
                    :boundary="panesDim.height"
                    split="horiz"
                    :minPercent="queryPaneMinPctHeight"
                    :maxPercent="queryPaneMaxPctHeight"
                    :deactivatedMaxPctZone="
                        queryPaneMaxPctHeight - (100 - queryPaneMaxPctHeight) * 2
                    "
                >
                    <template slot="pane-left">
                        <mxs-split-pane
                            v-model="editorPanePctWidth"
                            class="editor__content"
                            :minPercent="editorPaneMinPctWidth"
                            :maxPercent="
                                100 - $helpers.pxToPct({ px: 100, containerPx: panesDim.width })
                            "
                            :boundary="panesDim.width"
                            split="vert"
                            :disable="isChartMaximized || !showVisChart"
                        >
                            <template slot="pane-left">
                                <sql-editor
                                    ref="sqlEditor"
                                    v-model="allQueryTxt"
                                    class="editor pt-2 pl-2"
                                    :completionItems="completionItems"
                                    isKeptAlive
                                    :isTabMoveFocus.sync="isTabMoveFocus"
                                    @on-selection="SET_SELECTED_QUERY_TXT($event)"
                                    @shortkey="eventBus.$emit('shortkey', $event)"
                                />
                            </template>
                            <template slot="pane-right">
                                <chart-pane
                                    v-if="showVisChart"
                                    v-model="chartOpt"
                                    :containerHeight="chartContainerHeight"
                                    :chartTypes="SQL_CHART_TYPES"
                                    :axisTypes="CHART_AXIS_TYPES"
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
                    v-if="isVisSidebarShown"
                    v-model="chartOpt"
                    :chartTypes="SQL_CHART_TYPES"
                    :axisTypes="CHART_AXIS_TYPES"
                    :queryModes="QUERY_MODES"
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
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-03-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapMutations, mapState } from 'vuex'
import Editor from '@wsModels/Editor'
import QueryEditor from '@wsModels/QueryEditor'
import QueryResult from '@wsModels/QueryResult'
import SchemaSidebar from '@wsModels/SchemaSidebar'
import TxtEditorToolbarCtr from '@wkeComps/QueryEditor/TxtEditorToolbarCtr.vue'
import SqlEditor from '@wsComps/SqlEditor'
import QueryResultCtr from '@wkeComps/QueryEditor/QueryResultCtr.vue'
import ChartConfig from '@wkeComps/QueryEditor/ChartConfig'
import ChartPane from '@wkeComps/QueryEditor/ChartPane'
import { EventBus } from '@wkeComps/QueryEditor/EventBus'

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
        queryTab: { type: Object, required: true },
    },
    data() {
        return {
            // mxs-split-pane states
            minMainPanePct: 0,
            editorPanePctWidth: 100,
            mouseDropDOM: null, // mouse drop DOM node
            mouseDropWidget: null, // mouse drop widget while dragging to editor
            visSidebarWidth: 250,
            txtEditorToolbarHeight: 28,
            // chart-config and chart-pane state
            defChartOpt: {
                type: '',
                chartData: { datasets: [], labels: [] },
                axisKeys: { x: '', y: '' },
                axesType: { x: '', y: '' },
                tableData: [],
                hasTrendline: false,
                isHorizChart: false,
                isMaximized: false,
            },
            chartOpt: null,
        }
    },
    computed: {
        ...mapState({
            query_snippets: state => state.prefAndStorage.query_snippets,
            query_pane_pct_height: state => state.prefAndStorage.query_pane_pct_height,
            CMPL_SNIPPET_KIND: state => state.mxsWorkspace.config.CMPL_SNIPPET_KIND,
            SQL_CHART_TYPES: state => state.mxsWorkspace.config.SQL_CHART_TYPES,
            CHART_AXIS_TYPES: state => state.mxsWorkspace.config.CHART_AXIS_TYPES,
            QUERY_MODES: state => state.mxsWorkspace.config.QUERY_MODES,
            tab_moves_focus: state => state.prefAndStorage.tab_moves_focus,
        }),
        eventBus() {
            return EventBus
        },
        isVisSidebarShown() {
            return Editor.getters('getIsVisSidebarShown')
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
            let resSets = []
            // user query result data
            const userQueryResults = this.$helpers.stringifyClone(
                this.$typy(QueryResult.getters('getActiveUserQueryRes'), 'data.attributes.results')
                    .safeArray
            )
            let resSetCount = 0
            for (const res of userQueryResults) {
                if (res.data) {
                    ++resSetCount
                    resSets.push({ id: `RESULT SET ${resSetCount}`, ...res })
                }
            }
            // preview data
            const { PRVW_DATA, PRVW_DATA_DETAILS } = this.QUERY_MODES
            const prvwModes = [PRVW_DATA, PRVW_DATA_DETAILS]
            const activePrvwNode = SchemaSidebar.getters('getActivePrvwNode')
            for (const mode of prvwModes) {
                const data = this.$helpers.stringifyClone(
                    this.$typy(
                        QueryResult.getters('getActivePrvwData')(mode),
                        'data.attributes.results[0]'
                    ).safeObjectOrEmpty
                )
                if (!this.$typy(data).isEmptyObject) {
                    let resName = ''
                    switch (mode) {
                        case PRVW_DATA:
                            resName = this.$mxs_t('previewData')
                            break
                        case PRVW_DATA_DETAILS:
                            resName = this.$mxs_t('viewDetails')
                            break
                    }
                    resSets.push({
                        id: `${resName} of ${activePrvwNode.qualified_name}`,
                        ...data,
                    })
                }
            }
            return resSets
        },
        snippetList() {
            return this.query_snippets.map(q => ({
                label: q.name,
                detail: `SNIPPET - ${q.sql}`,
                insertText: q.sql,
                type: this.CMPL_SNIPPET_KIND,
            }))
        },
        completionItems() {
            return [...SchemaSidebar.getters('getSchemaCompletionItems'), ...this.snippetList]
        },
        showVisChart() {
            const labels = this.$typy(this.chartOpt, 'chartData.labels').safeArray
            return Boolean(this.$typy(this.chartOpt, 'type').safeString && labels.length)
        },
        isChartMaximized() {
            return this.$typy(this.chartOpt, 'isMaximized').safeBoolean
        },
        panesDim() {
            return { width: this.dim.width, height: this.dim.height - this.txtEditorToolbarHeight }
        },
        chartContainerHeight() {
            return (this.panesDim.height * this.queryPanePctHeight) / 100
        },
        visSidebarPct() {
            return this.$helpers.pxToPct({
                px: this.visSidebarWidth,
                containerPx: this.panesDim.width,
            })
        },
        mainPanePct() {
            if (this.isVisSidebarShown) return 100 - this.visSidebarPct
            return 100
        },
        queryPaneMinPctHeight() {
            return this.$helpers.pxToPct({ px: 26, containerPx: this.panesDim.height })
        },
        queryPaneMaxPctHeight() {
            return 100 - this.queryPaneMinPctHeight
        },
        queryPanePctHeight: {
            get() {
                return this.query_pane_pct_height
            },
            set(v) {
                this.SET_QUERY_PANE_PCT_HEIGHT(v)
            },
        },
        editorPaneMinPctWidth() {
            return this.showVisChart
                ? this.$helpers.pxToPct({ px: 32, containerPx: this.panesDim.width })
                : 0
        },
        allQueryTxt: {
            get() {
                return Editor.getters('getQueryTxt')
            },
            set(value) {
                Editor.update({
                    where: QueryEditor.getters('getActiveQueryTabId'),
                    data: {
                        query_txt: value,
                    },
                })
            },
        },
        resultPaneDim() {
            const visSideBarWidth = this.isVisSidebarShown ? this.visSidebarWidth : 0
            return {
                width: this.panesDim.width - visSideBarWidth,
                height: (this.panesDim.height * (100 - this.queryPanePctHeight)) / 100,
            }
        },
    },
    watch: {
        isChartMaximized(v) {
            if (v) this.editorPanePctWidth = this.editorPaneMinPctWidth
            else this.editorPanePctWidth = 50
        },
        showVisChart(v) {
            if (v) this.editorPanePctWidth = 50
            else this.editorPanePctWidth = 100
        },
    },
    created() {
        this.setDefChartOptState()
    },
    methods: {
        ...mapMutations({
            SET_SELECTED_QUERY_TXT: 'editorsMem/SET_SELECTED_QUERY_TXT',
            SET_QUERY_PANE_PCT_HEIGHT: 'prefAndStorage/SET_QUERY_PANE_PCT_HEIGHT',
            SET_TAB_MOVES_FOCUS: 'prefAndStorage/SET_TAB_MOVES_FOCUS',
        }),
        toggleTabMoveFocus() {
            if (!this.$typy(this.$refs, 'sqlEditor.editor').isEmptyObject)
                this.$refs.sqlEditor.editor.trigger('', 'editor.action.toggleTabFocusMode')
        },
        setDefChartOptState() {
            this.chartOpt = this.$helpers.lodash.cloneDeep(this.defChartOpt)
        },
        // editor related functions
        placeToEditor(text) {
            this.$refs.sqlEditor.insertAtCursor({ text })
        },
        handleGenMouseDropWidget(dropTarget) {
            /**
             *  Setting text cursor to all elements as a fallback method for firefox
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
                    //remove the prev cursor widget first then add
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
