<template>
    <div v-resize.quiet="setPanelsPct" class="fill-height">
        <div
            ref="paneContainer"
            class="query-page d-flex flex-column fill-height"
            :class="{ 'query-page--fullscreen': isFullScreen }"
        >
            <toolbar-container
                ref="toolbarContainer"
                :queryTxt="queryTxt"
                :selectedQueryTxt="selectedQueryTxt"
                :isFullScreen="isFullScreen"
            />
            <split-pane
                v-if="minSidebarPct"
                v-model="sidebarPct"
                class="query-page__content"
                :minPercent="minSidebarPct"
                split="vert"
                :disable="isCollapsed"
            >
                <template slot="pane-left">
                    <sidebar-container
                        @get-curr-prvw-data-schemaId="previewDataSchemaId = $event"
                        @is-fullscreen="isFullScreen = $event"
                        @is-collapsed="isCollapsed = $event"
                        @place-to-editor="placeToEditor"
                    />
                </template>
                <template slot="pane-right">
                    <!-- Main panel -->
                    <split-pane v-model="editorPct" split="horiz" :minPercent="minEditorPct">
                        <template slot="pane-left">
                            <query-editor
                                ref="queryEditor"
                                v-model="queryTxt"
                                class="editor pt-2 pl-2"
                                :cmplList="getDbCmplList"
                                @on-selection="selectedQueryTxt = $event"
                                @onCtrlEnter="() => $refs.toolbarContainer.onRun('all')"
                                @onCtrlShiftEnter="() => $refs.toolbarContainer.onRun('selected')"
                            />
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
 * Change Date: 2025-03-24
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
export default {
    name: 'query-view',
    components: {
        'query-editor': QueryEditor,
        SidebarContainer,
        QueryResult,
        ToolbarContainer,
    },
    data() {
        return {
            minSidebarPct: 0,
            sidebarPct: 0,
            editorPct: 60,
            minEditorPct: 0,
            isFullScreen: false,
            isCollapsed: false,
            resultPaneDim: {
                height: 0,
                width: 0,
            },
            queryTxt: '',
            previewDataSchemaId: '',
            selectedQueryTxt: '',
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
        isFullScreen() {
            this.$nextTick(() => {
                this.handleSetSidebarPct({ isCollapsed: this.isCollapsed })
                this.handleSetMinEditorPct()
            })
        },
        isCollapsed(v) {
            this.$nextTick(() => this.handleSetSidebarPct({ isCollapsed: v }))
        },
        sidebarPct() {
            this.$nextTick(() => this.setResultPaneDim())
        },
        editorPct() {
            this.$nextTick(() => this.setResultPaneDim())
        },
    },
    async created() {
        await this.checkActiveConn()
        if (this.active_conn_state) await this.checkActiveDb()
    },
    async beforeDestroy() {
        if (this.curr_cnct_resource) await this.disconnect()
    },
    mounted() {
        this.$help.doubleRAF(() => this.setPanelsPct())
    },
    methods: {
        ...mapActions({
            disconnect: 'query/disconnect',
            checkActiveConn: 'query/checkActiveConn',
            checkActiveDb: 'query/checkActiveDb',
        }),
        setPanelsPct() {
            this.handleSetSidebarPct({ isCollapsed: this.isCollapsed })
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
        handleSetSidebarPct({ isCollapsed }) {
            const containerWidth = this.$refs.paneContainer.clientWidth
            if (isCollapsed) {
                this.minSidebarPct = this.pxToPct({ px: 40, containerPx: containerWidth })
                this.sidebarPct = this.minSidebarPct
            } else {
                this.minSidebarPct = this.pxToPct({ px: 200, containerPx: containerWidth })
                this.sidebarPct = this.pxToPct({ px: 240, containerPx: containerWidth })
            }
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
.query-result {
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
