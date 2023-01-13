<template>
    <v-stepper
        v-model="activeStageIdx"
        class="data-migration-stepper d-flex flex-column mt-4"
        outlined
    >
        <div
            class="d-flex flex-wrap align-stretch justify-space-between mxs-color-helper border-bottom-table-border"
        >
            <v-stepper-step
                v-for="(stage, stageIdx) in stages"
                :key="stageIdx"
                :step="stageIdx"
                :complete="stage.isComplete"
            >
                {{ stage.name }}
            </v-stepper-step>
        </div>
        <v-stepper-items class="fill-height">
            <v-stepper-content
                v-for="(stage, stageIdx) in stages"
                :key="stageIdx"
                :step="stageIdx"
                class="fill-height"
            >
                <div
                    v-if="stageIdx === activeStageIdx"
                    class="stage-container pa-6 fill-height d-flex flex-column justify-space-between"
                >
                    <v-form ref="form" v-model="isFormValid" lazy-validation>
                        <component :is="stage.component" ref="stageComponent" />
                    </v-form>
                    <etl-stage-btns
                        class="mt-4"
                        :step="stageIdx"
                        :isPrevDisabled="isPrevDisabled"
                        :isNextDisabled="isNextDisabled"
                        @prev="prev"
                        @next="next"
                    />
                </div>
            </v-stepper-content>
        </v-stepper-items>
    </v-stepper>
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
import EtlTask from '@queryEditorSrc/store/orm/models/EtlTask'
import QueryConn from '@queryEditorSrc/store/orm/models/QueryConn'
import EtlObjSelectCtr from '@queryEditorSrc/components/EtlObjSelectCtr.vue'
import EtlConnsCtr from '@queryEditorSrc/components/EtlConnsCtr.vue'
import EtlStageBtns from '@queryEditorSrc/components/EtlStageBtns.vue'
import { mapState } from 'vuex'

export default {
    name: 'etl-stage-ctr',
    components: {
        EtlObjSelectCtr,
        EtlConnsCtr,
        EtlStageBtns,
    },
    data() {
        return {
            isFormValid: true,
        }
    },
    computed: {
        ...mapState({
            ETL_STAGE_INDEX: state => state.mxsWorkspace.config.ETL_STAGE_INDEX,
            QUERY_CONN_BINDING_TYPES: state => state.mxsWorkspace.config.QUERY_CONN_BINDING_TYPES,
        }),
        activeEtlTask() {
            return EtlTask.getters('getActiveEtlTaskWithRelation')
        },
        stages() {
            const { CONN, SRC_OBJ, OBJ_MIGR, DATA_MIGR } = this.ETL_STAGE_INDEX
            // TODO: Handle isComplete value
            return [
                {
                    name: this.$mxs_tc('connections', 1),
                    component: 'etl-conns-ctr',
                    isComplete: this.activeStageIdx > CONN,
                },
                {
                    name: this.$mxs_t('objSelection'),
                    component: 'etl-obj-select-ctr',
                    isComplete: this.activeStageIdx > SRC_OBJ,
                },
                {
                    name: this.$mxs_t('objMigration'),
                    component: 'div', //  TODO: Replace with the objects migration component
                    isComplete: this.activeStageIdx > OBJ_MIGR,
                },
                {
                    name: this.$mxs_t('dataMigration'),
                    component: 'div', //  TODO: Replace with the data migration report component
                    isComplete: this.activeStageIdx > DATA_MIGR,
                },
            ]
        },
        activeStageIdx: {
            get() {
                return this.activeEtlTask.active_stage_index
            },
            set(v) {
                EtlTask.update({
                    where: this.activeEtlTask.id,
                    data: { active_stage_index: v },
                })
            },
        },
        isPrevDisabled() {
            const { SRC_OBJ } = this.ETL_STAGE_INDEX
            switch (this.activeStageIdx) {
                case SRC_OBJ:
                    //Disable "previous" button if ETl already has source and destination connections
                    return this.$typy(this.activeEtlTask, 'connections').safeArray.length === 2
                default:
                    return false
            }
        },
        isNextDisabled() {
            return !this.isFormValid
        },
    },
    watch: {
        async activeStageIdx() {
            // Reset validation after changing the stage
            await this.$refs.form[0].resetValidation()
        },
    },
    methods: {
        prev() {
            this.activeStageIdx--
        },
        /**
         * TODO: handle shown connections open error.
         * Right now it's shown automatically in app snackbar because the requests
         * are called via $queryHttp axios
         */
        async handleOpenConns(currentStage) {
            const etl_task_id = this.activeEtlTask.id
            const { src, dest } = this.$refs.stageComponent[currentStage].$data
            await QueryConn.dispatch('openEtlConn', {
                body: {
                    target: 'odbc',
                    connection_string: src.connection_string,
                },
                binding_type: this.QUERY_CONN_BINDING_TYPES.ETL_SRC,
                etl_task_id,
                meta: { src_type: src.type },
            })
            await QueryConn.dispatch('openEtlConn', {
                body: dest,
                binding_type: this.QUERY_CONN_BINDING_TYPES.ETL_DEST,
                etl_task_id,
                meta: { dest_name: dest.target },
            })
        },
        async next(currentStage) {
            await this.$refs.form[0].validate()
            let isStageComplete = false
            if (this.isFormValid) {
                const { CONN } = this.ETL_STAGE_INDEX
                switch (currentStage) {
                    case CONN: {
                        await this.handleOpenConns(currentStage)
                        isStageComplete = true
                        break
                    }
                }
                if (isStageComplete) this.activeStageIdx++
            }
        },
    },
}
</script>

<style lang="scss">
.data-migration-stepper {
    .v-stepper__content {
        padding: 0px;
    }
    .v-stepper__wrapper {
        height: 100%;
        .stage-container {
            overflow-y: auto;
        }
    }
}
</style>
