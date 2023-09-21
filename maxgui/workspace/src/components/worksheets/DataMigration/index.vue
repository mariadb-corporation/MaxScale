<template>
    <v-tabs v-model="activeStageIdx" vertical class="v-tabs--mariadb v-tabs--etl" hide-slider eager>
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
 * Change Date: 2027-09-19
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
import { mapState } from 'vuex'

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
        ...mapState({
            ETL_STATUS: state => state.mxsWorkspace.config.ETL_STATUS,
        }),
        task() {
            return EtlTask.getters('findRecord')(this.taskId)
        },
        hasEtlRes() {
            return Boolean(EtlTask.getters('findResTables')(this.taskId).length)
        },
        areConnsAlive() {
            return QueryConn.getters('areActiveEtlConnsAlive')
        },
        isPreparingEtl() {
            return this.$typy(this.task, 'is_prepare_etl').safeBoolean
        },
        isMigrationDisabled() {
            if (this.isPreparingEtl) return !this.areConnsAlive
            return !this.hasEtlRes
        },
        stages() {
            const { RUNNING, COMPLETE } = this.ETL_STATUS
            const { status } = this.task
            return [
                {
                    name: this.$mxs_t('overview'),
                    component: 'etl-overview-stage',
                    isDisabled: false,
                },
                {
                    name: this.$mxs_tc('connections', 1),
                    component: 'etl-conns-stage',
                    isDisabled: this.areConnsAlive || status === COMPLETE || status === RUNNING,
                },
                {
                    name: this.$mxs_t('objSelection'),
                    component: 'etl-obj-select-stage',
                    isDisabled: !this.areConnsAlive || status === COMPLETE || status === RUNNING,
                },
                {
                    name: this.$mxs_t('migration'),
                    component: 'etl-migration-stage',
                    isDisabled: this.isMigrationDisabled,
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

<style lang="scss">
.v-tabs--mariadb.v-tabs--etl {
    .v-slide-group__wrapper {
        border-bottom: none !important;
        .v-slide-group__content {
            align-items: flex-start !important;
        }
    }
}
.v-tabs--etl {
    .v-tab {
        height: 42px !important;
        width: 100%;
        .tab-name {
            letter-spacing: normal;
        }
        &:hover {
            background: #eefafd;
        }
        &--active {
            .tab-name {
                background-color: $separator;
                color: $blue-azure !important;
                border-radius: 8px;
            }
        }
    }
}
</style>
