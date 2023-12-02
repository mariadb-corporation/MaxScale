<template>
    <mxs-stage-ctr>
        <template v-slot:header>
            <div class="etl-migration-stage__header">
                <div class="d-flex align-center">
                    <h3
                        class="mxs-stage-ctr__title mxs-color-helper text-navigation font-weight-light"
                        data-test="stage-header-title"
                    >
                        {{ $mxs_t('migration') }}
                    </h3>
                    <etl-migration-manage
                        v-if="!isPrepareEtl"
                        :task="task"
                        @on-restart="onRestart"
                    />
                </div>
                <div class="header-text my-4">
                    <etl-status-icon
                        :icon="$typy(task, 'status').safeString"
                        :spinning="isRunning"
                        class="mb-1"
                        data-test="header-status-icon"
                    />
                    <span
                        v-if="isPrepareEtl && !isRunning"
                        class="mxs-color-helper text-navigation"
                        data-test="prepare-script-info"
                    >
                        {{ prepareScriptInfo }}
                    </span>
                    <span
                        v-if="generalErr"
                        class="mxs-color-helper text-navigation"
                        data-test="general-err"
                    >
                        {{ generalErr }}
                    </span>
                    <span
                        v-else-if="hasErrAtCreationStage"
                        class="mxs-color-helper text-navigation"
                        data-test="creation-stage-err"
                    >
                        {{ $mxs_t(`errors.etl_create_stage`) }}
                    </span>
                    <span
                        v-else-if="!isPrepareEtl"
                        class="mxs-color-helper text-navigation"
                        data-test="fallback-msg"
                    >
                        {{ $mxs_t($typy(task, 'status').safeString.toLowerCase()) }}
                        <template v-if="isRunning">...</template>
                    </span>
                </div>
            </div>
        </template>
        <template v-slot:body>
            <v-container fluid class="fill-height">
                <v-progress-linear
                    v-if="isPrepareEtl && isRunning"
                    indeterminate
                    color="primary"
                    class="align-self-start"
                />
                <etl-logs
                    v-else-if="!etlResTable.length && isInErrState"
                    :task="task"
                    class="fill-height"
                />
                <etl-tbl-script
                    v-else
                    class="migration-tbl"
                    :task="task"
                    :data="etlResTable"
                    :headers="tableHeaders"
                    :custom-sort="customSort"
                    @get-activeRow="activeItem = $event"
                    @get-staging-data="stagingScript = $event"
                >
                    <template
                        v-for="slot in ['schema', 'table']"
                        v-slot:[`item.${slot}`]="{ value }"
                    >
                        <mxs-truncate-str :key="slot" :tooltipItem="{ txt: `${value}` }" />
                    </template>
                    <template v-slot:[`item.obj`]="{ item }">
                        <mxs-truncate-str :tooltipItem="{ txt: `${customCol(item, 'obj')}` }" />
                    </template>
                    <template v-slot:[`item.result`]="{ item }">
                        <div class="d-flex align-center flex-row">
                            <etl-status-icon
                                :icon="objMigrationStatus(item).icon"
                                :spinning="objMigrationStatus(item).isSpinning"
                            />
                            <mxs-truncate-str
                                :tooltipItem="{ txt: objMigrationStatus(item).txt }"
                            />
                        </div>
                    </template>
                </etl-tbl-script>
            </v-container>
        </template>

        <template v-if="!isRunning" v-slot:footer>
            <div
                class="etl-migration-stage__footer d-flex flex-column flex-grow-1"
                :class="{ 'etl-migration-stage__footer--with-log': isOutputMsgShown }"
                data-test="stage-footer"
            >
                <template v-if="isOutputMsgShown">
                    <h6 class="mxs-color-helper text-navigation">
                        {{ $mxs_t('outputMsgs') }}
                    </h6>
                    <code
                        class="fill-height overflow-y-auto mariadb-code-style rounded mxs-color-helper all-border-separator pa-4 text-wrap output-msg-ctr"
                        data-test="output-msg-ctr"
                    >
                        <template v-if="activeItem">
                            {{
                                activeItem.error ||
                                    (hasErrAtCreationStage
                                        ? $mxs_t('warnings.objCreation')
                                        : objMigrationStatus(activeItem).txt)
                            }}
                            <br />
                            <template v-if="$typy(activeItem, 'execution_time').isDefined">
                                {{ $mxs_t('exeTime') }}:
                                {{
                                    $mxs_tc('seconds', activeItem.execution_time === 1 ? 1 : 2, {
                                        value: activeItem.execution_time,
                                    })
                                }}
                            </template>
                        </template>
                    </code>
                </template>
                <v-btn
                    v-if="isPrepareEtl && !isOutputMsgShown"
                    small
                    height="36"
                    color="primary"
                    class="mt-auto font-weight-medium px-7 text-capitalize start-btn"
                    rounded
                    depressed
                    :disabled="Boolean(generalErr) || isRunning || isInErrState"
                    data-test="start-migration-btn"
                    @click="start"
                >
                    {{ $mxs_t('startMigration') }}
                </v-btn>
            </div>
        </template>
    </mxs-stage-ctr>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import EtlTask from '@wsModels/EtlTask'
import EtlTblScript from '@wkeComps/DataMigration/EtlTblScript.vue'
import EtlStatusIcon from '@wkeComps/DataMigration/EtlStatusIcon.vue'
import EtlMigrationManage from '@wkeComps/DataMigration/EtlMigrationManage.vue'
import EtlLogs from '@wkeComps/DataMigration/EtlLogs.vue'
import { mapState } from 'vuex'

export default {
    name: 'etl-migration-stage',
    components: {
        EtlTblScript,
        EtlStatusIcon,
        EtlMigrationManage,
        EtlLogs,
    },
    props: { task: { type: Object, required: true }, srcConn: { type: Object, required: true } },
    data() {
        return {
            stagingScript: [],
            activeItem: null,
        }
    },
    computed: {
        ...mapState({
            ETL_STATUS: state => state.mxsWorkspace.config.ETL_STATUS,
            ETL_API_STAGES: state => state.mxsWorkspace.config.ETL_API_STAGES,
        }),
        taskId() {
            return this.task.id
        },
        etlRes() {
            return EtlTask.getters('findEtlRes')(this.taskId)
        },
        etlResTable() {
            return EtlTask.getters('findResTables')(this.taskId)
        },
        migrationStage() {
            return EtlTask.getters('findResStage')(this.taskId)
        },
        tableHeaders() {
            return this.isPrepareEtl
                ? [
                      { text: 'SCHEMA', value: 'schema', cellClass: 'truncate-cell', width: '50%' },
                      { text: 'TABLE', value: 'table', cellClass: 'truncate-cell', width: '50%' },
                  ]
                : [
                      { text: 'OBJECT', value: 'obj', cellClass: 'truncate-cell', width: '60%' },
                      { text: 'RESULT', value: 'result', cellClass: 'truncate-cell', width: '40%' },
                  ]
        },
        isRunning() {
            return this.task.status === this.ETL_STATUS.RUNNING
        },
        isInErrState() {
            return this.task.status === this.ETL_STATUS.ERROR
        },
        queryId() {
            return this.$typy(this.task, 'meta.async_query_id').safeString
        },
        isPrepareEtl() {
            return this.$typy(this.task, 'is_prepare_etl').safeBoolean
        },
        hasErrAtCreationStage() {
            return this.isInErrState && this.migrationStage === this.ETL_API_STAGES.CREATE
        },
        generalErr() {
            return this.$typy(this.etlRes, 'error').safeString
        },
        isOutputMsgShown() {
            if (this.isPrepareEtl) {
                if (!this.$typy(this.etlRes, 'ok').isDefined) return false
                return !this.$typy(this.etlRes, 'ok').safeBoolean
            }
            return true
        },
        prepareScriptInfo() {
            return this.$mxs_t(
                this.isInErrState
                    ? 'errors.failedToPrepareMigrationScript'
                    : 'info.migrationScriptInfo'
            )
        },
    },
    watch: {
        queryId: {
            immediate: true,
            async handler(v) {
                if (v && this.srcConn.id) {
                    await EtlTask.dispatch('getEtlCallRes', this.task.id)
                }
            },
        },
    },
    methods: {
        async cancel() {
            await EtlTask.dispatch('cancelEtlTask', this.task.id)
        },
        objMigrationStatus(item) {
            let icon = this.ETL_STATUS.RUNNING,
                isSpinning = this.isRunning,
                txt = `${item.rows || 0} rows migrated`
            if (item.error) {
                icon = this.ETL_STATUS.ERROR
                isSpinning = false
                txt = this.$mxs_t('error')
            } else if (item.execution_time) {
                icon = this.ETL_STATUS.COMPLETE
                isSpinning = false
                if (this.hasErrAtCreationStage) {
                    icon = { value: '$vuetify.icons.mxs_alertWarning', color: 'warning' }
                    txt = this.$mxs_t('warnings.objCreation')
                }
            }
            return { icon, isSpinning, txt }
        },
        customCol(item, key) {
            switch (key) {
                case 'obj':
                    return `\`${item.schema}\`.\`${item.table}\``
                case 'result':
                    return this.objMigrationStatus(item).txt
                case 'schema':
                    return item.schema
                case 'table':
                    return item.table
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
            await EtlTask.dispatch('handleEtlCall', { id, tables: this.stagingScript })
        },
        async start() {
            const id = this.task.id
            EtlTask.update({
                where: id,
                data(obj) {
                    obj.is_prepare_etl = false
                },
            })
            await EtlTask.dispatch('handleEtlCall', { id, tables: this.stagingScript })
        },
    },
}
</script>
<style lang="scss">
.migration-tbl {
    .truncate-cell {
        // Workaround for enabling auto-truncate on mxs-truncate-str
        max-width: 1px;
        //mxs-truncate-str span
        span {
            vertical-align: middle;
        }
    }
}
</style>
<style lang="scss" scoped>
.etl-migration-stage__header {
    .header-text {
        font-size: 0.875rem;
    }
}
.etl-migration-stage__footer {
    &--with-log {
        min-height: 150px;
        max-height: 200px;
        .output-msg-ctr {
            font-size: 0.75rem;
            flex: 1 1 auto;
        }
    }

    .start-btn {
        width: 135px;
    }
}
</style>
