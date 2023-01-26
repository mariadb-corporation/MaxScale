<template>
    <v-sheet class="d-flex flex-column fill-height mt-12">
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
                    @click="createTask"
                >
                    + {{ $mxs_t(`etlOps.actions.${ETL_ACTIONS.CREATE}`) }}
                </v-btn>
            </div>
        </portal>
        <data-table
            class="fill-height"
            :headers="tableHeaders"
            :data="tableRows"
            :search="search_keyword"
            sortBy="created"
            showAll
        >
            <template v-slot:name="{ data: { item } }">
                <span class="mxs-color-helper pointer text-anchor" @click="viewTask(item)">
                    {{ item.name }}
                </span>
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
            <template v-slot:menu="{ data: { item } }">
                <etl-task-manage
                    :id="item.id"
                    :types="actionTypes"
                    content-class="v-menu--mariadb v-menu--mariadb-with-shadow-no-border"
                >
                    <template v-slot:activator="{ on, attrs }">
                        <v-btn icon v-bind="attrs" v-on="on">
                            <v-icon size="18" color="deep-ocean">
                                mdi-dots-horizontal
                            </v-icon>
                        </v-btn>
                    </template>
                </etl-task-manage>
            </template>
        </data-table>
    </v-sheet>
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
import EtlTask from '@workspaceSrc/store/orm/models/EtlTask'
import QueryConn from '@workspaceSrc/store/orm/models/QueryConn'
import EtlTaskManage from '@workspaceSrc/components/EtlTaskManage.vue'

export default {
    name: 'etl-tasks',
    components: { EtlTaskManage },
    computed: {
        ...mapState({
            search_keyword: 'search_keyword',
            ETL_ACTIONS: state => state.mxsWorkspace.config.ETL_ACTIONS,
        }),
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
            return EtlTask.all().map(t => ({
                ...t,
                created: this.$helpers.dateFormat({ value: t.created }),
                menu: t.id,
            }))
        },
        actionTypes() {
            const { CANCEL, DELETE, DISCONNECT, VIEW } = this.ETL_ACTIONS
            return [CANCEL, DELETE, DISCONNECT, VIEW]
        },
    },
    async created() {
        await QueryConn.dispatch('validateConns', {
            persistentConns: QueryConn.getters('getEtlConns'),
        })
    },
    methods: {
        parseMeta(meta) {
            return {
                from: this.$typy(meta, 'src_type').safeString || 'Unknown',
                to: this.$typy(meta, 'dest_name').safeString || 'Unknown',
            }
        },
        async createTask() {
            await EtlTask.dispatch('insertEtlTask')
        },
        viewTask(item) {
            EtlTask.dispatch('viewEtlTask', item)
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
