<template>
    <page-wrapper>
        <portal to="page-header">
            <h4
                style="line-height: normal;"
                class="mb-0 mxs-color-helper text-navigation text-h4 text-capitalize"
            >
                {{ $mxs_t('dataMigration') }}
            </h4>
        </portal>
        <portal to="page-header--right">
            <div class="d-flex align-center">
                <global-search />
                <v-btn
                    width="160"
                    outlined
                    height="36"
                    rounded
                    class="ml-4 text-capitalize px-8 font-weight-medium"
                    depressed
                    small
                    color="accent-dark"
                    @click="create"
                >
                    + {{ actions[ETL_ACTIONS.CREATE].text }}
                </v-btn>
            </div>
        </portal>
        <v-sheet class="d-flex flex-column fill-height mt-12">
            <data-table
                class="fill-height"
                :headers="tableHeaders"
                :data="tableRows"
                :search="search_keyword"
                sortBy="id"
                showAll
            >
                <template v-slot:menu="{ data: { item } }">
                    <v-menu
                        transition="slide-y-transition"
                        offset-y
                        left
                        content-class="mariadb-select-v-menu mariadb-select-v-menu--full-border"
                    >
                        <template v-slot:activator="{ on, attrs }">
                            <v-btn icon v-bind="attrs" v-on="on">
                                <v-icon size="18" color="deep-ocean">
                                    mdi-dots-horizontal
                                </v-icon>
                            </v-btn>
                        </template>

                        <v-list>
                            <v-list-item
                                v-for="action in item.menu"
                                :key="action.text"
                                @click="actionHandler({ type: action.type, task: item })"
                            >
                                <v-list-item-title>{{ action.text }}</v-list-item-title>
                            </v-list-item>
                        </v-list>
                    </v-menu>
                </template>
                <template v-slot:meta="{ data: { item: { meta } } }">
                    <div class="d-flex">
                        {{ parseMeta(meta).from }}
                        <span class="mx-1 dashed-arrow d-inline-flex align-center">
                            <span class="line"></span>
                            <v-icon color="primary" size="12" class="arrow rotate-right">
                                $vuetify.icons.mxs_arrowHead
                            </v-icon>
                        </span>
                        {{ parseMeta(meta).to }}
                    </div>
                </template>
            </data-table>
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
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapState } from 'vuex'
import EtlTask from '@queryEditorSrc/store/orm/models/EtlTask'

export default {
    name: 'etl-tasks',
    computed: {
        ...mapState({
            search_keyword: 'search_keyword',
            QUERY_CONN_BINDING_TYPES: state =>
                state.queryEditorConfig.config.QUERY_CONN_BINDING_TYPES,
            ETL_ACTIONS: state => state.queryEditorConfig.config.ETL_ACTIONS,
        }),
        actions() {
            const { DELETE, CANCEL, CREATE } = this.ETL_ACTIONS
            return {
                [CREATE]: {
                    text: this.$mxs_t(`etlOps.actions.${CREATE}`),
                    type: CREATE,
                },
                [DELETE]: {
                    text: this.$mxs_t(`etlOps.actions.${DELETE}`),
                    type: DELETE,
                },
                [CANCEL]: {
                    text: this.$mxs_t(`etlOps.actions.${CANCEL}`),
                    type: CANCEL,
                },
            }
        },
        tableHeaders() {
            return [
                { text: 'Name', value: 'name' },
                { text: 'Status', value: 'status' },
                { text: 'Created', value: 'created' },
                { text: 'From->To', value: 'meta' },
                { text: '', value: 'menu', sortable: false, width: '1px' },
            ]
        },
        tableRows() {
            const { DELETE, CANCEL } = this.ETL_ACTIONS
            return EtlTask.all().map(t => ({
                ...t,
                menu: [this.actions[CANCEL], this.actions[DELETE]],
            }))
        },
    },
    methods: {
        create() {
            //TODO: Open ETL task form
            EtlTask.dispatch('insertEtlTask')
        },
        parseMeta(meta) {
            const { ETL_DEST, ETL_SRC } = this.QUERY_CONN_BINDING_TYPES
            return { from: meta[ETL_SRC] || 'Unknown', to: meta[ETL_DEST] || 'Unknown' }
        },
        /**
         * @param {String} param.type - delete||cancel
         * @param {Object} param.task - task
         */
        async actionHandler({ type, task }) {
            const { DELETE, CANCEL } = this.ETL_ACTIONS
            switch (type) {
                case DELETE:
                    EtlTask.delete(task.id)
                    break
                case CANCEL:
                    //TODO: Call cancelEtlTask
                    break
            }
        },
    },
}
</script>
<style lang="scss" scoped>
.dashed-arrow {
    .line {
        border-bottom: 2px dashed $primary;
        width: 22px;
    }
}
</style>
