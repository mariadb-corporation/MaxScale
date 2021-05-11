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
            <v-toolbar
                outlined
                elevation="0"
                height="50"
                class="query-page__header border-bottom-none"
                :class="{ 'query-page__header--fullscreen': isFullScreen }"
            >
                <v-toolbar-title class="color text-navigation text-capitalize">
                    {{ $route.name }}
                </v-toolbar-title>

                <v-spacer></v-spacer>
                <connection-manager />
                <v-btn
                    width="80"
                    outlined
                    height="36"
                    rounded
                    class="ml-4 text-capitalize px-8 font-weight-medium"
                    depressed
                    small
                    color="accent-dark"
                    :disabled="!queryTxt || !active_conn_state"
                    @click="onRun"
                >
                    {{ $t('run') }}
                </v-btn>
            </v-toolbar>
            <split-pane
                v-if="minSidebarPct"
                v-model="sidebarPct"
                class="query-page__content"
                :minPercent="minSidebarPct"
                split="vert"
                :disable="!isFullScreen || isCollapsed"
            >
                <!-- sidebar panel -->
                <template slot="pane-left">
                    <v-card
                        v-if="loading_db_tree"
                        class="fill-height db-tb-list"
                        :loading="loading_db_tree"
                    />
                    <v-fade-transition>
                        <db-list
                            v-if="!loading_db_tree"
                            class="db-tb-list"
                            :schemaList="db_tree"
                            :disabled="!active_conn_state"
                            @is-fullscreen="isFullScreen = $event"
                            @is-collapsed="isCollapsed = $event"
                            @reload-schema="loadSchema"
                            @preview-data="
                                schemaId =>
                                    handleFetchPreview({
                                        SQL_QUERY_MODE: SQL_QUERY_MODES.PRVW_DATA,
                                        schemaId,
                                    })
                            "
                            @view-details="
                                schemaId =>
                                    handleFetchPreview({
                                        SQL_QUERY_MODE: SQL_QUERY_MODES.PRVW_DATA_DETAILS,
                                        schemaId,
                                    })
                            "
                            @place-to-editor="placeToEditor"
                            @load-children="handleLoadChildren"
                        />
                    </v-fade-transition>
                </template>
                <template slot="pane-right">
                    <!-- Main panel -->
                    <split-pane v-model="editorPanePct" split="horiz" :minPercent="10">
                        <template slot="pane-left">
                            <query-editor
                                v-model="queryTxt"
                                class="editor pt-2 pl-2"
                                :tableDist="getDbCmplList"
                            />
                        </template>
                        <template slot="pane-right">
                            <query-result
                                ref="queryResultPane"
                                :dynDim="resultPaneDim"
                                class="query-result"
                                :previewDataSchemaId="previewDataSchemaId"
                                :queryTxt="queryTxt"
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
import DbList from './DbList'
import QueryResult from './QueryResult'
import { mapActions, mapState, mapMutations } from 'vuex'
import ConnectionManager from './ConnectionManager'
export default {
    name: 'query-view',
    components: {
        'query-editor': QueryEditor,
        DbList,
        QueryResult,
        ConnectionManager,
    },
    data() {
        return {
            queryTxt: '',
            minSidebarPct: 0,
            sidebarPct: 0,
            editorPanePct: 60,
            isFullScreen: false,
            isCollapsed: false,
            previewDataSchemaId: '',
            resultPaneDim: {
                height: 0,
                width: 0,
            },
        }
    },
    computed: {
        ...mapState({
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
            active_conn_state: state => state.query.active_conn_state,
            curr_query_mode: state => state.query.curr_query_mode,
            db_tree: state => state.query.db_tree,
            loading_db_tree: state => state.query.loading_db_tree,
            db_completion_list: state => state.query.db_completion_list,
            curr_cnct_resource: state => state.query.curr_cnct_resource,
        }),
        getDbCmplList() {
            // remove duplicated labels
            return this.$help.lodash.uniqBy(this.db_completion_list, 'label')
        },
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
    async created() {
        if (this.active_conn_state) await this.loadSchema()
    },
    async beforeDestroy() {
        if (this.curr_cnct_resource) await this.disconnect()
    },
    methods: {
        ...mapMutations({
            SET_CURR_QUERY_MODE: 'query/SET_CURR_QUERY_MODE',
        }),
        ...mapActions({
            fetchDbList: 'query/fetchDbList',
            fetchPrvw: 'query/fetchPrvw',
            fetchQueryResult: 'query/fetchQueryResult',
            clearDataPreview: 'query/clearDataPreview',
            fetchTables: 'query/fetchTables',
            fetchCols: 'query/fetchCols',
            disconnect: 'query/disconnect',
        }),
        setResultPaneDim() {
            if (this.$refs.queryResultPane) {
                const { clientWidth, clientHeight } = this.$refs.queryResultPane.$el
                this.resultPaneDim = {
                    width: clientWidth,
                    height: clientHeight,
                }
            }
        },
        async loadSchema() {
            await this.fetchDbList()
        },
        async handleLoadChildren(item) {
            if (!item.id.includes('.')) await this.fetchTables(item)
            else await this.fetchCols(item)
        },
        //TODO: move all bounding pct calculation to another component
        getSidebarBoundingPct({ isMin }) {
            const maxContainerWidth = this.$refs.wrapperContainer.clientWidth
            let minWidth = isMin ? 200 : 273 // sidebar width in px
            if (this.isCollapsed) minWidth = 40
            const minPercent = (minWidth / maxContainerWidth) * 100
            return minPercent
        },
        handleSetSidebarPct({ isCollapsed }) {
            this.minSidebarPct = this.getSidebarBoundingPct({ isMin: true })
            if (isCollapsed) this.sidebarPct = this.minSidebarPct
            else this.sidebarPct = this.getSidebarBoundingPct({ isMin: false })
        },
        placeToEditor(schemaId) {
            this.queryTxt = `${this.queryTxt} ${schemaId}`
        },
        onResize() {
            this.handleSetSidebarPct({ isCollapsed: this.isCollapsed })
        },
        async onRun() {
            this.SET_CURR_QUERY_MODE(this.SQL_QUERY_MODES.QUERY_VIEW)
            await this.fetchQueryResult(this.queryTxt)
        },
        // For table type only
        async handleFetchPreview({ SQL_QUERY_MODE, schemaId }) {
            this.previewDataSchemaId = schemaId
            this.clearDataPreview()
            this.SET_CURR_QUERY_MODE(SQL_QUERY_MODE)
            switch (SQL_QUERY_MODE) {
                case this.SQL_QUERY_MODES.PRVW_DATA:
                case this.SQL_QUERY_MODES.PRVW_DATA_DETAILS:
                    await this.fetchPrvw({
                        tblId: this.previewDataSchemaId,
                        prvwMode: SQL_QUERY_MODE,
                    })
                    break
            }
        },
    },
}
</script>

<style lang="scss" scoped>
.editor,
.db-tb-list,
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
    &__header {
        &--fullscreen {
            margin-left: 0px !important;
        }
    }
}
</style>
