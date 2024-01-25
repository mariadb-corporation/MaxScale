<template>
    <div class="d-flex flex-column fill-height">
        <txt-editor-toolbar-ctr
            class="d-flex"
            :height="txtEditorToolbarHeight"
            :queryTab="queryTab"
            :queryTabTmp="queryTabTmp"
            :queryTabConn="queryTabConn"
            :queryTxt="queryTxt"
            :isVisSidebarShown="isVisSidebarShown"
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
                <!-- TxtEditor pane contains editor and result pane -->
                <mxs-split-pane
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
                                <mxs-sql-editor
                                    ref="sqlEditor"
                                    v-model="queryTxt"
                                    class="editor pt-2 pl-2"
                                    :completionItems="completionItems"
                                    isKeptAlive
                                    :isTabMoveFocus.sync="isTabMoveFocus"
                                    @on-selection="SET_SELECTED_QUERY_TXT($event)"
                                    @shortkey="eventBus.$emit('workspace-shortkey', $event)"
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
                            :dim="resultPaneDim"
                            class="query-result-ctr"
                            :queryTab="queryTab"
                            :queryTabConn="queryTabConn"
                            :queryTabTmp="queryTabTmp"
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
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapMutations, mapState, mapGetters } from 'vuex'
import TxtEditor from '@wsModels/TxtEditor'
import QueryConn from '@wsModels/QueryConn'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import TxtEditorToolbarCtr from '@wkeComps/QueryEditor/TxtEditorToolbarCtr.vue'
import QueryResultCtr from '@wkeComps/QueryEditor/QueryResultCtr.vue'
import ChartConfig from '@wkeComps/QueryEditor/ChartConfig'
import ChartPane from '@wkeComps/QueryEditor/ChartPane'
import { EventBus } from '@wkeComps/EventBus'
import schemaNodeHelper from '@wsSrc/utils/schemaNodeHelper'
import { QUERY_MODES, SQL_CHART_TYPES, CHART_AXIS_TYPES } from '@wsSrc/constants'

export default {
    name: 'txt-editor-ctr',
    components: {
        TxtEditorToolbarCtr,
        QueryResultCtr,
        ChartConfig,
        ChartPane,
    },
    props: {
        dim: { type: Object, required: true },
        queryEditorTmp: { type: Object, required: true },
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
            query_pane_pct_height: state => state.prefAndStorage.query_pane_pct_height,
            tab_moves_focus: state => state.prefAndStorage.tab_moves_focus,
            identifier_auto_completion: state => state.prefAndStorage.identifier_auto_completion,
        }),
        ...mapGetters({ snippetCompletionItems: 'prefAndStorage/snippetCompletionItems' }),
        eventBus() {
            return EventBus
        },
        queryTabConn() {
            return QueryConn.getters('findQueryTabConn')(this.queryTab.id)
        },
        queryTabTmp() {
            return QueryTabTmp.find(this.queryTab.id) || {}
        },
        prvwDataResultSets() {
            let resSets = []
            const { prvw_data, prvw_data_details, previewing_node } = this.queryTabTmp
            const nodeQualifiedName = this.$typy(previewing_node, 'qualified_name').safeString
            const addToResSets = (data, mode) => {
                if (!this.$typy(data).isEmptyObject)
                    resSets.push({ id: `${this.$mxs_t(mode)} of ${nodeQualifiedName}`, ...data })
            }
            addToResSets(prvw_data, 'previewData')
            addToResSets(prvw_data_details, 'viewDetails')
            return resSets
        },
        userResultSets() {
            const results = this.$typy(this.queryTabTmp, 'query_results.data.attributes.results')
                .safeArray
            let count = 0
            return results.reduce((resultSets, res) => {
                if (res.data) {
                    ++count
                    resultSets.push({ id: `RESULT SET ${count}`, ...res })
                }
                return resultSets
            }, [])
        },
        resultSets() {
            return [...this.userResultSets, ...this.prvwDataResultSets]
        },
        activeSchema() {
            return this.$typy(this.queryTabConn, 'active_db').safeString
        },
        schemaTree() {
            let tree = this.$typy(this.queryEditorTmp, 'db_tree').safeArray
            if (this.identifier_auto_completion && this.activeSchema)
                return tree.filter(n => n.qualified_name !== this.activeSchema)
            return tree
        },
        schemaTreeCompletionItems() {
            return schemaNodeHelper.genNodeCompletionItems(this.schemaTree)
        },
        activeSchemaIdentifierCompletionItems() {
            return this.$typy(this.queryTabTmp, 'schema_identifier_names_completion_items')
                .safeArray
        },
        completionItems() {
            return [
                ...this.schemaTreeCompletionItems,
                ...this.activeSchemaIdentifierCompletionItems,
                ...this.snippetCompletionItems,
            ]
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
        txtEditor() {
            return TxtEditor.find(this.queryTab.id) || {}
        },
        queryTxt: {
            get() {
                return this.$typy(this.txtEditor, 'query_txt').safeString
            },
            set(value) {
                TxtEditor.update({ where: this.queryTab.id, data: { query_txt: value } })
            },
        },
        resultPaneDim() {
            const visSideBarWidth = this.isVisSidebarShown ? this.visSidebarWidth : 0
            return {
                width: this.panesDim.width - visSideBarWidth,
                height: (this.panesDim.height * (100 - this.queryPanePctHeight)) / 100,
            }
        },
        isVisSidebarShown() {
            return this.$typy(this.txtEditor, 'is_vis_sidebar_shown').safeBoolean
        },
        isTabMoveFocus: {
            get() {
                return this.tab_moves_focus
            },
            set(v) {
                this.SET_TAB_MOVES_FOCUS(v)
            },
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
        this.QUERY_MODES = QUERY_MODES
        this.SQL_CHART_TYPES = SQL_CHART_TYPES
        this.CHART_AXIS_TYPES = CHART_AXIS_TYPES
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
