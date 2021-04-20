<template>
    <div
        ref="wrapperContainer"
        v-resize="onResize"
        class="fill-height"
        :class="{ 'wrapper-container': !isFullScreen }"
    >
        <div class="query-page fill-height" :class="{ 'query-page--fullscreen': isFullScreen }">
            <div
                class="page-header d-flex ml-n1"
                :class="{ 'page-header--fullscreen': isFullScreen }"
            >
                <div class="d-flex align-center">
                    <div class="d-inline-flex align-center">
                        <h4
                            style="line-height: normal;"
                            class="ml-1 mb-0 color text-navigation display-1 text-capitalize"
                        >
                            {{ $route.name }}
                        </h4>
                    </div>
                </div>
                <v-spacer />
                <div class="d-flex flex-wrap ">
                    <v-btn
                        width="80"
                        outlined
                        height="36"
                        rounded
                        class="text-capitalize px-8 font-weight-medium"
                        depressed
                        small
                        color="accent-dark"
                        @click="onRun"
                    >
                        {{ $t('run') }}
                    </v-btn>
                </div>
            </div>
            <v-sheet
                class="fill-height"
                :class="[!isFullScreen ? 'pt-6 pb-8' : 'panels--fullscreen']"
            >
                <split-pane
                    v-model="sidebarPct"
                    :minPercent="minSidebarPct"
                    split="vert"
                    :disable="!isFullScreen || isCollapsed"
                >
                    <template slot="pane-left">
                        <db-list
                            class="db-tb-list"
                            :schemaList="schema.schemaList"
                            @is-fullscreen="isFullScreen = $event"
                            @is-collapsed="isCollapsed = $event"
                            @reload-schema="loadSchema"
                            @preview-data="previewData"
                            @view-details="viewDetails"
                            @place-to-editor="placeToEditor"
                        />
                    </template>
                    <template slot="pane-right">
                        <split-pane v-model="editorPanePct" split="horiz" :minPercent="10">
                            <template slot="pane-left">
                                <query-editor
                                    v-model="queryTxt"
                                    class="editor pt-2 pl-2"
                                    :tableDist="distArr"
                                />
                            </template>
                            <template slot="pane-right">
                                <query-result class="query-result pb-3" />
                            </template>
                        </split-pane>
                    </template>
                </split-pane>
            </v-sheet>
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
import { mapActions, mapState } from 'vuex'

export default {
    name: 'query-view',
    components: {
        'query-editor': QueryEditor,
        DbList,
        QueryResult,
    },
    data() {
        return {
            queryTxt: '',
            minSidebarPct: 0,
            sidebarPct: 0,
            editorPanePct: 70,
            isFullScreen: false,
            isCollapsed: false,
        }
    },
    computed: {
        ...mapState({
            conn_schema: state => state.query.conn_schema,
            loading_schema: state => state.query.loading_schema,
        }),
        distArr: function() {
            return this.schema.schemaFlatList
        },
        schema() {
            let res = { schemaList: [], schemaFlatList: [] }
            if (this.loading_schema) return res
            const { schemas = [] } = this.conn_schema
            res.schemaList = schemas.map(({ name: schemaId, tables = [] }) => {
                res.schemaFlatList.push({
                    label: schemaId,
                    detail: 'SCHEMA',
                    insertText: schemaId,
                    type: 'schema',
                })
                return {
                    type: 'schema',
                    name: schemaId,
                    id: schemaId,
                    children: tables.map(({ name: tableName, columns = [] }) => {
                        const tableId = `${schemaId}.${tableName}`
                        res.schemaFlatList.push({
                            label: tableName,
                            detail: 'TABLE',
                            insertText: tableName,
                            type: 'table',
                        })
                        return {
                            type: 'table',
                            name: tableName,
                            id: tableId,
                            level: 1,
                            children: columns.map(({ name: columnName, dataType }) => {
                                res.schemaFlatList.push({
                                    label: columnName,
                                    insertText: columnName,
                                    detail: 'COLUMN',
                                    type: 'column',
                                })
                                return {
                                    type: 'column',
                                    name: columnName,
                                    dataType: dataType,
                                    id: `${tableId}.${columnName}`,
                                    level: 2,
                                }
                            }),
                        }
                    }),
                }
            })
            // remove duplicated labels
            res.schemaFlatList = this.$help.lodash.uniqBy(res.schemaFlatList, 'label')
            return res
        },
    },
    watch: {
        isFullScreen() {
            this.$nextTick(() => this.handleSetSidebarPct({ isCollapsed: this.isCollapsed }))
        },
        isCollapsed(v) {
            this.$nextTick(() => this.handleSetSidebarPct({ isCollapsed: v }))
        },
    },
    async created() {
        await this.loadSchema()
    },
    methods: {
        ...mapActions({
            fetchConnectionSchema: 'query/fetchConnectionSchema',
            fetchPreviewData: 'query/fetchPreviewData',
            fetchDataDetails: 'query/fetchDataDetails',
            fetchQueryResult: 'query/fetchQueryResult',
        }),
        async loadSchema() {
            await this.fetchConnectionSchema()
        },
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
            await this.fetchQueryResult(this.queryTxt)
        },
        // For table type only
        async previewData(schemaId) {
            const query = `SELECT * FROM ${schemaId};`
            await this.fetchPreviewData(query)
        },
        async viewDetails(schemaId) {
            const query = `DESCRIBE ${schemaId};`
            await this.fetchDataDetails(query)
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
    transition: all 0.2s cubic-bezier(0.4, 0, 0.2, 1);
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
    .page-header {
        &--fullscreen {
            margin-left: 0px !important;
            padding: 16px 16px 16px 8px;
        }
    }
    $page-header-height: 70px; // including empty space
    .panels--fullscreen {
        height: calc(100% - #{$page-header-height});
    }
}
</style>
