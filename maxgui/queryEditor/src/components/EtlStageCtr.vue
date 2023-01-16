<template>
    <v-tabs v-model="activeStageIdx" vertical class="v-tabs--mariadb v-tabs--etl" hide-slider eager>
        <v-tab
            v-for="(stage, stageIdx) in stages"
            :key="stageIdx"
            class="my-1 justify-space-between align-center"
        >
            <div class="tab-name pa-2 mxs-color-helper text-navigation font-weight-regular">
                {{ stage.name }}
            </div>
        </v-tab>
        <v-tabs-items v-model="activeStageIdx" class="fill-height">
            <v-tab-item
                v-for="(stage, stageIdx) in stages"
                :key="stageIdx"
                class="fill-height ml-8"
            >
                <div
                    v-if="stageIdx === activeStageIdx"
                    class="fill-height d-flex flex-column justify-space-between"
                >
                    <v-form
                        ref="form"
                        v-model="isFormValid"
                        lazy-validation
                        class="form-container fill-height"
                    >
                        <component :is="stage.component" ref="stageComponent" />
                    </v-form>
                    <etl-stage-btns
                        class="px-6 py-3"
                        :step="stageIdx"
                        :isPrevDisabled="isPrevDisabled"
                        :isNextDisabled="isNextDisabled"
                        :isLoading="isLoading"
                        @prev="prev"
                        @next="next"
                    />
                </div>
            </v-tab-item>
        </v-tabs-items>
    </v-tabs>
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
/* eslint-disable vue/no-unused-components */
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
            isStageComplete: false,
            isLoading: false,
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
        hasActiveConns() {
            return this.$typy(this.activeEtlTask, 'connections').safeArray.length === 2
        },
        isPrevDisabled() {
            const { SRC_OBJ } = this.ETL_STAGE_INDEX
            switch (this.activeStageIdx) {
                case SRC_OBJ:
                    //Disable "previous" button if ETl already has source and destination connections
                    return this.hasActiveConns
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
        async validateForm() {
            await this.$refs.form[0].validate()
        },
        /**
         * TODO: handle shown connections open error.
         * Right now it's shown automatically in app snackbar because the requests
         * are called via $queryHttp axios
         */
        async handleOpenConns() {
            const etl_task_id = this.activeEtlTask.id
            const { src, dest } = this.$refs.stageComponent[0].$data
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
            this.isLoading = false
            this.isStageComplete = this.hasActiveConns
        },
        async next(currentStage) {
            this.isStageComplete = false
            await this.validateForm()
            if (this.isFormValid) {
                const { CONN } = this.ETL_STAGE_INDEX
                this.isLoading = true
                switch (currentStage) {
                    case CONN: {
                        await this.handleOpenConns()
                        break
                    }
                }
                if (this.isStageComplete) this.activeStageIdx++
            }
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
        .form-container {
            overflow-y: auto;
        }
    }
}
</style>
