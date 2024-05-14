<template>
    <mxs-data-table
        class="etl-tasks-table"
        :headers="tableHeaders"
        :items="tableRows"
        sortBy="created"
        :items-per-page="-1"
        fixed-header
        hide-default-footer
        :height="height"
    >
        <template v-slot:[`item.name`]="{ item }">
            <span class="mxs-color-helper pointer text-anchor" @click="viewTask(item)">
                {{ item.name }}
            </span>
        </template>

        <template v-slot:[`item.status`]="{ value }">
            <div class="d-flex align-center">
                <etl-status-icon :icon="value" :spinning="value === ETL_STATUS.RUNNING" />
                {{ value }}
                <span v-if="value === ETL_STATUS.RUNNING">...</span>
            </div>
        </template>

        <template v-slot:[`item.meta`]="{ value }">
            <div class="d-flex">
                {{ parseMeta(value).from }}
                <span class="mx-1 dashed-arrow d-inline-flex align-center">
                    <span class="line"></span>
                    <v-icon color="primary" size="12" class="arrow rotate-right">
                        $vuetify.icons.mxs_arrowHead
                    </v-icon>
                </span>
                {{ parseMeta(value).to }}
            </div>
        </template>
        <template v-slot:[`item.menu`]="{ item, value }">
            <etl-task-manage
                :task="item"
                :types="actionTypes"
                @input="activeItemMenu = $event ? value : null"
            >
                <template v-slot:activator="{ on, attrs }">
                    <v-btn
                        icon
                        small
                        class="etl-task-menu-btn"
                        :class="{ 'etl-task-menu-btn--visible': activeItemMenu === value }"
                        v-bind="attrs"
                        v-on="on"
                    >
                        <v-icon size="18" color="navigation">
                            mdi-dots-horizontal
                        </v-icon>
                    </v-btn>
                </template>
            </etl-task-manage>
        </template>
    </mxs-data-table>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import EtlTask from '@wsModels/EtlTask'
import EtlTaskManage from '@wsComps/EtlTaskManage.vue'
import EtlStatusIcon from '@wkeComps/DataMigration/EtlStatusIcon.vue'
import { ETL_ACTIONS, ETL_STATUS } from '@wsSrc/constants'

export default {
    name: 'etl-tasks',
    components: { EtlTaskManage, EtlStatusIcon },
    props: {
        height: { type: Number, required: true },
    },
    data() {
        return {
            activeItemMenu: null,
        }
    },
    computed: {
        tableHeaders() {
            return [
                { text: 'Task Name', value: 'name' },
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
            const { CANCEL, DELETE, DISCONNECT, VIEW } = ETL_ACTIONS
            return [CANCEL, DELETE, DISCONNECT, VIEW]
        },
    },
    created() {
        this.ETL_STATUS = ETL_STATUS
    },

    methods: {
        parseMeta(meta) {
            return {
                from: this.$typy(meta, 'src_type').safeString || 'Unknown',
                to: this.$typy(meta, 'dest_name').safeString || 'Unknown',
            }
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
.etl-tasks-table {
    .etl-task-menu-btn {
        visibility: hidden;
        &--visible {
            visibility: visible;
        }
    }
    tbody tr:hover {
        .etl-task-menu-btn {
            visibility: visible;
        }
    }
}
</style>
