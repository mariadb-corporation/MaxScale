<template>
    <etl-stage-ctr>
        <template v-slot:header>
            <div class="etl-migration-script-stage__header">
                <div class="d-flex align-center">
                    <h3 class="etl-stage-title mxs-color-helper text-navigation font-weight-light">
                        {{ $mxs_t('migrationProgress') }}
                    </h3>

                    <etl-task-manage
                        :id="activeEtlTask.id"
                        v-model="isMenuOpened"
                        :types="actionTypes"
                        content-class="v-menu--mariadb v-menu--mariadb-with-shadow-no-border"
                        @on-restart="onRestart"
                    >
                        <template v-slot:activator="{ on, attrs }">
                            <v-btn
                                small
                                height="30"
                                color="primary"
                                class="ml-4 font-weight-medium px-4 text-capitalize"
                                rounded
                                depressed
                                outlined
                                v-bind="attrs"
                                v-on="on"
                            >
                                {{ $mxs_t('manage') }}

                                <v-icon
                                    :class="[isMenuOpened ? 'rotate-up' : 'rotate-down']"
                                    size="14"
                                    class="mr-0 ml-1"
                                    left
                                >
                                    $vuetify.icons.mxs_arrowDown
                                </v-icon>
                            </v-btn>
                        </template>
                    </etl-task-manage>
                </div>
                <div class="mt-4">
                    <etl-status-icon
                        :icon="activeEtlTask.status"
                        :isRunning="isRunning"
                        class="mb-1"
                    />
                    <span v-if="hasErrAtCreationStage" class="mxs-color-helper text-navigation">
                        {{ $mxs_t(`errors.etl_create_stage`) }}
                    </span>

                    <span v-else class="mxs-color-helper text-navigation">
                        {{ $mxs_t(activeEtlTask.status.toLowerCase()) }}
                        <span v-if="isRunning">...</span>
                    </span>
                </div>
            </div>
        </template>
        <template v-slot:body>
            <etl-tbl-script
                :data="getMigrationResTable"
                :headers="tableHeaders"
                :custom-sort="customSort"
                @get-activeRow="activeItem = $event"
                @get-staging-data="stagingMigrationScript = $event"
            >
                <template v-slot:[`item.obj`]="{ item }">
                    {{ customCol(item, 'obj') }}
                </template>
                <template v-slot:[`item.result`]="{ item }">
                    <etl-status-icon :icon="objStatus(item)" />
                    {{ customCol(item, 'result') }}
                </template>
            </etl-tbl-script>
        </template>
        <template v-slot:footer>
            <div class="etl-migration-report-stage__footer d-flex flex-column flex-grow-1">
                <h6 class="mxs-color-helper text-navigation">
                    {{ $mxs_t('outputMsgs') }}
                </h6>
                <code
                    class="fill-height overflow-y-auto mariadb-code-style rounded mxs-color-helper all-border-separator pa-4 text-wrap msg-log-ctr"
                >
                    <template v-if="activeItem">
                        {{
                            activeItem.error
                                ? activeItem.error
                                : hasErrAtCreationStage
                                ? $mxs_t('warnings.objCreation')
                                : $mxs_t('success.scriptExec')
                        }}
                    </template>
                </code>
            </div>
        </template>
    </etl-stage-ctr>
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
import EtlTask from '@wsModels/EtlTask'
import EtlStageCtr from '@wkeComps/DataMigration/EtlStageCtr.vue'
import EtlTblScript from '@wkeComps/DataMigration/EtlTblScript.vue'
import EtlStatusIcon from '@wkeComps/DataMigration/EtlStatusIcon.vue'
import EtlTaskManage from '@wsComps/EtlTaskManage.vue'
import { mapActions, mapState, mapGetters } from 'vuex'

export default {
    name: 'etl-migration-report-stage',
    components: {
        EtlStageCtr,
        EtlTblScript,
        EtlStatusIcon,
        EtlTaskManage,
    },
    data() {
        return {
            stagingMigrationScript: [],
            activeItem: null,
            isMenuOpened: false,
        }
    },
    computed: {
        ...mapState({
            ETL_STATUS: state => state.mxsWorkspace.config.ETL_STATUS,
            ETL_STAGE_INDEX: state => state.mxsWorkspace.config.ETL_STAGE_INDEX,
            ETL_API_STAGES: state => state.mxsWorkspace.config.ETL_API_STAGES,
            ETL_ACTIONS: state => state.mxsWorkspace.config.ETL_ACTIONS,
            are_conns_alive: state => state.etlMem.are_conns_alive,
        }),
        ...mapGetters({
            getMigrationResTable: 'etlMem/getMigrationResTable',
            getMigrationStage: 'etlMem/getMigrationStage',
        }),
        activeEtlTask() {
            return EtlTask.getters('getActiveEtlTaskWithRelation')
        },
        isLoading() {
            return this.$typy(this.activeEtlTask, 'meta.is_loading').safeBoolean
        },
        tableHeaders() {
            return [
                { text: 'OBJECT', value: 'obj' },
                { text: 'RESULT', value: 'result' },
            ]
        },
        isRunning() {
            return this.activeEtlTask.status === this.ETL_STATUS.RUNNING
        },
        queryId() {
            return this.$typy(this.activeEtlTask, 'meta.async_query_id').safeString
        },
        isActive() {
            return this.activeEtlTask.active_stage_index === this.ETL_STAGE_INDEX.DATA_MIGR
        },
        actionTypes() {
            const { CANCEL, DELETE, DISCONNECT, RESTART } = this.ETL_ACTIONS
            return [CANCEL, DELETE, DISCONNECT, RESTART]
        },
        hasErrAtCreationStage() {
            return (
                this.activeEtlTask.status === this.ETL_STATUS.ERROR &&
                this.getMigrationStage === this.ETL_API_STAGES.CREATE
            )
        },
    },
    watch: {
        queryId: {
            immediate: true,
            async handler(v) {
                if (v && this.isActive) {
                    this.validateActiveEtlTaskConns()
                    await this.getEtlCallRes(this.activeEtlTask.id)
                }
            },
        },
    },
    methods: {
        ...mapActions({
            getEtlCallRes: 'etlMem/getEtlCallRes',
            validateActiveEtlTaskConns: 'etlMem/validateActiveEtlTaskConns',
            handleEtlCall: 'etlMem/handleEtlCall',
        }),
        async cancel() {
            await EtlTask.dispatch('cancelEtlTask', this.activeEtlTask.id)
        },
        objStatus(item) {
            return item.error
                ? this.ETL_STATUS.ERROR
                : this.hasErrAtCreationStage
                ? { value: '$vuetify.icons.mxs_alertWarning', color: 'warning' }
                : this.ETL_STATUS.COMPLETE
        },
        customCol(item, key) {
            switch (key) {
                case 'obj':
                    return `\`${item.schema}\`.\`${item.table}\``
                case 'result':
                    return item.error
                        ? this.$mxs_t('error')
                        : this.hasErrAtCreationStage
                        ? this.$mxs_t('warnings.objCreation')
                        : this.$mxs_t('success.scriptExec')

                default:
                    return ''
            }
        },
        customSort(items, sortBy, sortDesc) {
            const isDesc = sortDesc[0]
            const sortByKey = sortBy[0]
            return items.sort((a, b) => {
                let colA = this.customCol(a, sortByKey),
                    colB = this.customCol(b, sortByKey)
                if (isDesc) return colB < colA ? -1 : 1
                return colA < colB ? -1 : 1
            })
        },
        async onRestart(id) {
            /**
             * TODO: Show a dialog with an option for preparing script again. e.g. The users
             * can change `create_mode`
             */
            this.validateActiveEtlTaskConns()
            if (this.are_conns_alive)
                await this.handleEtlCall({ id, tables: this.stagingMigrationScript })
        },
    },
}
</script>
<style lang="scss" scoped>
.etl-migration-report-stage__footer {
    min-height: 150px;
    max-height: 200px;
    .msg-log-ctr {
        font-size: 0.75rem;
        flex: 1 1 auto;
    }
}
</style>
