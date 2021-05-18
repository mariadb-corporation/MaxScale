<template>
    <div
        ref="wrapperContainer"
        v-resize="onResize"
        class="fill-height"
        :class="{ 'wrapper-container': !isFullScreen }"
    >
        <div
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
                :disable="!isFullScreen || isCollapsed"
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
                    <split-pane v-model="editorPanePct" split="horiz" :minPercent="10">
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
            editorPanePct: 60,
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
            curr_cnct_resource: state => state.query.curr_cnct_resource,
        }),
        ...mapGetters({
            getDbCmplList: 'query/getDbCmplList',
        }),
    },
    watch: {
        isFullScreen() {
            this.$nextTick(() => this.handleSetSidebarPct({ isCollapsed: this.isCollapsed }))
        },
        isCollapsed(v) {
            this.$nextTick(() => this.handleSetSidebarPct({ isCollapsed: v }))
        },
        sidebarPct() {
            this.$nextTick(() => this.setResultPaneDim())
        },
        editorPanePct() {
            this.$nextTick(() => this.setResultPaneDim())
        },
    },
    async beforeDestroy() {
        if (this.curr_cnct_resource) await this.disconnect()
    },
    methods: {
        ...mapActions({
            disconnect: 'query/disconnect',
        }),
        onResize() {
            this.handleSetSidebarPct({ isCollapsed: this.isCollapsed })
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
        handleSetSidebarPct({ isCollapsed }) {
            this.minSidebarPct = this.getSidebarBoundingPct({ isMin: true })
            if (isCollapsed) this.sidebarPct = this.minSidebarPct
            else this.sidebarPct = this.getSidebarBoundingPct({ isMin: false })
        },
        getSidebarBoundingPct({ isMin }) {
            const maxContainerWidth = this.$refs.wrapperContainer.clientWidth
            let minWidth = isMin ? 200 : 273 // sidebar width in px
            if (this.isCollapsed) minWidth = 40
            const minPercent = (minWidth / maxContainerWidth) * 100
            return minPercent
        },
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
    /* TODO: transition time affects the process of getting dim through ref
     * Add delay when calculating dim
     */
    /*   transition: all 0.2s cubic-bezier(0.4, 0, 0.2, 1); */
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
