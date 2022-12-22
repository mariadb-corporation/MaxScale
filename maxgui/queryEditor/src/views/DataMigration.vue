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
                    @click="actionHandler({ type: ETL_ACTIONS.CREATE })"
                >
                    + {{ actionMap[ETL_ACTIONS.CREATE].text }}
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
                        content-class="v-menu--mariadb v-menu--mariadb-full-border"
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
                                <v-list-item-title
                                    class="mxs-color-helper"
                                    :class="[
                                        action.type === ETL_ACTIONS.DELETE
                                            ? 'text-error'
                                            : 'text-text',
                                    ]"
                                >
                                    {{ action.text }}
                                </v-list-item-title>
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
import QueryConn from '@queryEditorSrc/store/orm/models/QueryConn'

export default {
    name: 'etl-tasks',
    computed: {
        ...mapState({
            search_keyword: 'search_keyword',
            QUERY_CONN_BINDING_TYPES: state =>
                state.queryEditorConfig.config.QUERY_CONN_BINDING_TYPES,
            ETL_ACTIONS: state => state.queryEditorConfig.config.ETL_ACTIONS,
        }),
        actionMap() {
            return Object.keys(this.ETL_ACTIONS).reduce((obj, key) => {
                const value = this.ETL_ACTIONS[key]
                obj[value] = {
                    text: this.$mxs_t(`etlOps.actions.${value}`),
                    type: value,
                }
                return obj
            }, {})
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
            const { CREATE } = this.ETL_ACTIONS
            return EtlTask.all().map(t => ({
                ...t,
                created: this.$helpers.dateFormat({ value: t.created }),
                menu: Object.values(this.actionMap).filter(o => o.type !== CREATE),
            }))
        },
    },
    methods: {
        parseMeta(meta) {
            const { ETL_DEST, ETL_SRC } = this.QUERY_CONN_BINDING_TYPES
            return { from: meta[ETL_SRC] || 'Unknown', to: meta[ETL_DEST] || 'Unknown' }
        },
        async disconnectConnsFromTask(task) {
            await this.$helpers.to(
                Promise.all(
                    EtlTask.getters('getEtlConnsByTaskId')(task.id).map(({ id }) =>
                        QueryConn.dispatch('disconnect', { id })
                    )
                )
            )
        },
        /**
         * @param {String} param.type - delete||cancel
         * @param {Object} param.task - task
         */
        async actionHandler({ type, task }) {
            const { CANCEL, CREATE, DELETE, DISCONNECT, VIEW } = this.ETL_ACTIONS
            switch (type) {
                case CANCEL:
                    await EtlTask.dispatch('cancelEtlTask', task.id)
                    break
                case CREATE:
                    //TODO: Open ETL task form (mode creation)
                    EtlTask.dispatch('insertEtlTask')
                    break
                case DELETE:
                    EtlTask.delete(task.id)
                    await this.disconnectConnsFromTask(task)
                    break
                case DISCONNECT:
                    await this.disconnectConnsFromTask(task)
                    break
                case VIEW:
                    //TODO: Open ETL task form (mode view or edit, depends on the status of the task)
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
