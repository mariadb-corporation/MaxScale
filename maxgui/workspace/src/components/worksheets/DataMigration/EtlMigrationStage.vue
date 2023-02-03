<template>
    <etl-stage-ctr>
        <template v-slot:header>
            <div class="etl-migration-stage__header">
                <div class="d-flex align-center">
                    <h3 class="etl-stage-title mxs-color-helper text-navigation font-weight-light">
                        {{ $mxs_t('migration') }}
                    </h3>
                    <etl-migration-manage
                        v-if="!isPrepareEtl"
                        :taskId="$typy(activeEtlTask, 'id').safeString"
                        @on-restart="onRestart"
                    />
                </div>
                <div class="header-text my-4">
                    <etl-status-icon
                        :icon="$typy(activeEtlTask, 'status').safeString"
                        :spinning="isRunning"
                        class="mb-1"
                    />
                    <span
                        v-if="isPrepareEtl && !isRunning"
                        class="mxs-color-helper text-navigation"
                    >
                        {{
                            $mxs_t(
                                isInErrState
                                    ? 'errors.failedToPrepareMigrationScript'
                                    : 'info.migrationScriptInfo'
                            )
                        }}
                    </span>
                    <span v-if="generalErr" class="mxs-color-helper text-navigation">
                        {{ generalErr }}
                    </span>
                    <span
                        v-else-if="hasErrAtCreationStage"
                        class="mxs-color-helper text-navigation"
                    >
                        {{ $mxs_t(`errors.etl_create_stage`) }}
                    </span>
                    <span v-else-if="!isPrepareEtl" class="mxs-color-helper text-navigation">
                        {{ $mxs_t($typy(activeEtlTask, 'status').safeString.toLowerCase()) }}
                        <span v-if="isRunning">...</span>
                    </span>
                </div>
            </div>
        </template>
        <template v-slot:body>
            <v-progress-linear
                v-if="isPrepareEtl && isRunning"
                indeterminate
                color="primary"
                class="align-self-start"
            />
            <etl-logs v-else-if="!getEtlResTable.length && isInErrState" class="fill-height" />
            <etl-tbl-script
                v-else
                :data="getEtlResTable"
                :headers="tableHeaders"
                :custom-sort="customSort"
                @get-activeRow="activeItem = $event"
                @get-staging-data="stagingScript = $event"
            >
                <template v-slot:[`item.obj`]="{ item }">
                    {{ customCol(item, 'obj') }}
                </template>
                <template v-slot:[`item.result`]="{ item }">
                    <etl-status-icon
                        :icon="objMigrationStatus(item).icon"
                        :spinning="objMigrationStatus(item).isSpinning"
                    />
                    {{ objMigrationStatus(item).txt }}
                </template>
            </etl-tbl-script>
        </template>

        <template v-if="!isRunning" v-slot:footer>
            <div
                class="etl-migration-stage__footer d-flex flex-column flex-grow-1"
                :class="{ 'etl-migration-stage__footer--with-log': showOutputLog }"
            >
                <template v-if="showOutputLog">
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
                                    : objMigrationStatus(activeItem).txt
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
                    v-if="isPrepareEtl && !showOutputLog"
                    small
                    height="36"
                    color="primary"
                    class="mt-auto font-weight-medium px-7 text-capitalize start-btn"
                    rounded
                    depressed
                    :disabled="Boolean(generalErr) || isRunning || isInErrState"
                    @click="start"
                >
                    {{ $mxs_t('startMigration') }}
                </v-btn>
            </div>
        </template>
    </etl-stage-ctr>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import EtlMigrationManage from '@wkeComps/DataMigration/EtlMigrationManage.vue'
import EtlLogs from '@wkeComps/DataMigration/EtlLogs.vue'
import { mapActions, mapMutations, mapState, mapGetters } from 'vuex'

export default {
    name: 'etl-migration-stage',
    components: {
        EtlStageCtr,
        EtlTblScript,
        EtlStatusIcon,
        EtlMigrationManage,
        EtlLogs,
    },
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
            etl_res: state => state.etlMem.etl_res,
        }),
        ...mapGetters({
            getEtlResTable: 'etlMem/getEtlResTable',
            getMigrationStage: 'etlMem/getMigrationStage',
            isSrcAlive: 'etlMem/isSrcAlive',
        }),
        activeEtlTask() {
            return EtlTask.getters('getActiveEtlTaskWithRelation')
        },
        activeEtlTaskId() {
            return this.activeEtlTask.id
        },
        tableHeaders() {
            return this.isPrepareEtl
                ? [
                      { text: 'SCHEMA', value: 'schema' },
                      { text: 'TABLE', value: 'table' },
                  ]
                : [
                      { text: 'OBJECT', value: 'obj' },
                      { text: 'RESULT', value: 'result' },
                  ]
        },
        isRunning() {
            return this.activeEtlTask.status === this.ETL_STATUS.RUNNING
        },
        isInErrState() {
            return this.activeEtlTask.status === this.ETL_STATUS.ERROR
        },
        queryId() {
            return this.$typy(this.activeEtlTask, 'meta.async_query_id').safeString
        },
        isPrepareEtl() {
            return this.$typy(this.activeEtlTask, 'is_prepare_etl').safeBoolean
        },
        hasErrAtCreationStage() {
            return this.isInErrState && this.getMigrationStage === this.ETL_API_STAGES.CREATE
        },
        generalErr() {
            return this.$typy(this.etl_res, 'error').safeString
        },
        showOutputLog() {
            if (this.isPrepareEtl) {
                if (!this.$typy(this.etl_res, 'ok').isDefined) return false
                return !this.$typy(this.etl_res, 'ok').safeBoolean
            }
            return true
        },
    },
    watch: {
        queryId: {
            immediate: true,
            async handler(v) {
                if (v && this.isSrcAlive) await this.getEtlCallRes(this.activeEtlTask.id)
            },
        },
        activeEtlTaskId: {
            immediate: true,
            handler() {
                this.SET_ETL_RES(null)
            },
        },
    },
    methods: {
        ...mapActions({
            getEtlCallRes: 'etlMem/getEtlCallRes',
            handleEtlCall: 'etlMem/handleEtlCall',
        }),
        ...mapMutations({
            SET_ETL_RES: 'etlMem/SET_ETL_RES',
        }),
        async cancel() {
            await EtlTask.dispatch('cancelEtlTask', this.activeEtlTask.id)
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
            await this.handleEtlCall({ id, tables: this.stagingScript })
        },
        async start() {
            EtlTask.update({
                where: this.activeEtlTask.id,
                data(obj) {
                    obj.is_prepare_etl = false
                },
            })
            await this.handleEtlCall({
                id: this.activeEtlTask.id,
                tables: this.stagingScript,
            })
        },
    },
}
</script>
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
        .msg-log-ctr {
            font-size: 0.75rem;
            flex: 1 1 auto;
        }
    }

    .start-btn {
        width: 135px;
    }
}
</style>
