<template>
    <v-tabs
        v-model="activeStageIdx"
        vertical
        class="v-tabs--mariadb v-tabs--mariadb--vert"
        hide-slider
        eager
    >
        <v-tab
            v-for="(stage, stageIdx) in stages"
            :key="stageIdx"
            :disabled="stage.isDisabled"
            class="my-1 justify-space-between align-center"
        >
            <div class="tab-name pa-2 mxs-color-helper text-navigation font-weight-regular">
                {{ stage.name }}
            </div>
        </v-tab>
        <v-tabs-items v-model="activeStageIdx" class="fill-height">
            <v-tab-item v-for="(stage, stageIdx) in stages" :key="stageIdx" class="fill-height">
                <component
                    :is="stage.component"
                    v-show="stageIdx === activeStageIdx"
                    :task="task"
                    v-bind="stage.props"
                />
            </v-tab-item>
        </v-tabs-items>
    </v-tabs>
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
import QueryConn from '@wsModels/QueryConn'
import EtlOverviewStage from '@wkeComps/DataMigration/EtlOverviewStage.vue'
import EtlConnsStage from '@wkeComps/DataMigration/EtlConnsStage.vue'
import EtlObjSelectStage from '@wkeComps/DataMigration/EtlObjSelectStage.vue'
import EtlMigrationStage from '@wkeComps/DataMigration/EtlMigrationStage.vue'
import { QUERY_CONN_BINDING_TYPES, ETL_STATUS } from '@wsSrc/constants'

export default {
    name: 'data-migration',
    components: {
        EtlOverviewStage,
        EtlConnsStage,
        EtlObjSelectStage,
        EtlMigrationStage,
    },
    props: { taskId: { type: String, required: true } },
    computed: {
        task() {
            return EtlTask.getters('findRecord')(this.taskId)
        },
        hasEtlRes() {
            return Boolean(EtlTask.getters('findResTables')(this.taskId).length)
        },
        conns() {
            return QueryConn.query()
                .where('etl_task_id', this.task.id)
                .get()
        },
        srcConn() {
            return this.conns.find(c => c.binding_type === QUERY_CONN_BINDING_TYPES.ETL_SRC) || {}
        },
        destConn() {
            return this.conns.find(c => c.binding_type === QUERY_CONN_BINDING_TYPES.ETL_DEST) || {}
        },
        hasConns() {
            return this.conns.length === 2
        },
        isPreparingEtl() {
            return this.$typy(this.task, 'is_prepare_etl').safeBoolean
        },
        isMigrationDisabled() {
            if (this.isPreparingEtl) return !this.hasConns
            return !this.hasEtlRes
        },
        stages() {
            const { RUNNING, COMPLETE } = ETL_STATUS
            const { status } = this.task
            const props = { task: this.task }
            return [
                {
                    name: this.$mxs_t('overview'),
                    component: 'etl-overview-stage',
                    isDisabled: false,
                    props: { ...props, hasConns: this.hasConns },
                },
                {
                    name: this.$mxs_tc('connections', 1),
                    component: 'etl-conns-stage',
                    isDisabled: this.hasConns || status === COMPLETE || status === RUNNING,
                    props: {
                        ...props,
                        srcConn: this.srcConn,
                        destConn: this.destConn,
                        hasConns: this.hasConns,
                    },
                },
                {
                    name: this.$mxs_t('objSelection'),
                    component: 'etl-obj-select-stage',
                    isDisabled: !this.hasConns || status === COMPLETE || status === RUNNING,
                    props,
                },
                {
                    name: this.$mxs_t('migration'),
                    component: 'etl-migration-stage',
                    isDisabled: this.isMigrationDisabled,
                    props: { ...props, srcConn: this.srcConn },
                },
            ]
        },
        activeStageIdx: {
            get() {
                return this.task.active_stage_index
            },
            set(v) {
                EtlTask.update({
                    where: this.task.id,
                    data: { active_stage_index: v },
                })
            },
        },
    },
}
</script>
