<template>
    <page-wrapper>
        <v-sheet class="pt-6 pb-8 fill-height">
            <page-header />
            <split-pane
                :minPercent="dbPaneMinPercent"
                :defaultPercent="dfDbPanePercent"
                split="vert"
                disable
            >
                <template slot="pane-left">
                    <db-list
                        :connSchema="conn_schema"
                        :loadingSchema="loading_schema"
                        class="db-tb-list"
                        @is-collapsed="handleDbListCollapse"
                        @reload-schema="loadSchema"
                        @preview-data="previewData"
                        @view-details="viewDetails"
                        @place-to-editor="placeToEditor"
                    />
                </template>
                <template slot="pane-right">
                    <split-pane split="horiz" :minPercent="10" :defaultPercent="70">
                        <template slot="pane-left">
                            <query-editor
                                v-model="value"
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
    </page-wrapper>
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
import PageHeader from './PageHeader'
import DbList from './DbList'
import QueryResult from './QueryResult'
import { mapActions, mapState } from 'vuex'

export default {
    name: 'query-view',
    components: {
        'query-editor': QueryEditor,
        PageHeader,
        DbList,
        QueryResult,
    },
    data() {
        return {
            dist: {}, // contains database name, table name and its columns
            value: '',
            dbPaneMinPercent: 20,
            dfDbPanePercent: 20,
        }
    },
    computed: {
        ...mapState({
            conn_schema: state => state.query.conn_schema,
            loading_schema: state => state.query.loading_schema,
        }),
        distArr: function() {
            let result = []
            //TODO: Flatten conn_schema
            return result
        },
    },
    async created() {
        await this.loadSchema()
    },
    methods: {
        ...mapActions({
            fetchConnectionSchema: 'query/fetchConnectionSchema',
        }),
        async loadSchema() {
            await this.fetchConnectionSchema()
        },
        handleDbListCollapse(v) {
            if (v) this.dfDbPanePercent = 3
            else this.dfDbPanePercent = 20
        },

        placeToEditor(schemaId) {
            this.value = `${this.value} ${schemaId}`
        },
        // For table type only
        previewData(schemaId) {
            const query = `SELECT * FROM ${schemaId};`
            //TODO: dispatch action to send query
            /* eslint-disable no-console */
            console.log('query', query)
        },
        viewDetails(schemaId) {
            const query = `DESCRIBE ${schemaId};`
            //TODO: dispatch action to send query
            /* eslint-disable no-console */
            console.log('query', query)
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
</style>
