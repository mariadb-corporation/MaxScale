<template>
    <etl-stage-ctr>
        <template v-slot:header>
            <div class="etl-migration-script-stage__header">
                <h3 class="etl-stage-title mxs-color-helper text-navigation font-weight-light">
                    {{ $mxs_t('migrationProgress') }}
                </h3>
                <div class="mt-4 d-flex align-center" :style="{ height: '30px' }">
                    <etl-status-icon :status="activeEtlTask.status" :isRunning="isRunning" />
                    <span class="mxs-color-helper text-navigation">
                        {{ $mxs_t(activeEtlTask.status.toLowerCase()) }}
                        <span v-if="isRunning">...</span>
                    </span>
                    <v-btn
                        v-if="isRunning"
                        small
                        height="30"
                        color="primary"
                        class="ml-4 font-weight-medium px-4 text-capitalize"
                        rounded
                        depressed
                        outlined
                        @click="cancel"
                    >
                        {{ $mxs_t('cancel') }}
                    </v-btn>
                </div>
            </div>
        </template>
        <template v-slot:body>
            <etl-migration-tbl
                :data="getMigrationResTable"
                :headers="tableHeaders"
                :stagingMigrationObjs.sync="stagingMigrationObjs"
                :custom-sort="customSort"
                @get-activeRow="activeItem = $event"
            >
                <template v-slot:[`item.obj`]="{ item }">
                    {{ customCol(item, 'obj') }}
                </template>
                <template v-slot:[`item.result`]="{ item }">
                    <etl-status-icon :status="objStatus(item)" />
                    {{ customCol(item, 'result') }}
                </template>
            </etl-migration-tbl>
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
                                : $mxs_t('info.scriptExecSuccessfully')
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
import EtlTask from '@queryEditorSrc/store/orm/models/EtlTask'
import EtlStageCtr from '@queryEditorSrc/components/EtlStageCtr.vue'
import EtlMigrationTbl from '@queryEditorSrc/components/EtlMigrationTbl.vue'
import EtlStatusIcon from '@queryEditorSrc/components/EtlStatusIcon.vue'
import { mapActions, mapState, mapGetters } from 'vuex'

export default {
    name: 'etl-migration-report-stage',
    components: {
        EtlStageCtr,
        EtlMigrationTbl,
        EtlStatusIcon,
    },
    data() {
        return {
            stagingMigrationObjs: [],
            activeItem: null,
        }
    },
    computed: {
        ...mapState({
            ETL_STATUS: state => state.mxsWorkspace.config.ETL_STATUS,
            ETL_STAGE_INDEX: state => state.mxsWorkspace.config.ETL_STAGE_INDEX,
        }),
        ...mapGetters({ getMigrationResTable: 'etlMem/getMigrationResTable' }),
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
    },
    watch: {
        queryId: {
            immediate: true,
            async handler(v) {
                if (v && this.isActive) {
                    await this.validateActiveEtlTaskConns()
                    await this.getEtlCallRes(this.activeEtlTask.id)
                }
            },
        },
    },
    methods: {
        ...mapActions({
            getEtlCallRes: 'etlMem/getEtlCallRes',
            validateActiveEtlTaskConns: 'etlMem/validateActiveEtlTaskConns',
        }),
        async cancel() {
            await EtlTask.dispatch('cancelEtlTask', this.activeEtlTask.id)
        },
        objStatus(item) {
            return item.error ? this.ETL_STATUS.ERROR : this.ETL_STATUS.COMPLETE
        },
        customCol(item, key) {
            switch (key) {
                case 'obj':
                    return `\`${item.schema}\`.\`${item.table}\``
                case 'result':
                    return item.error
                        ? this.$mxs_t('error')
                        : this.$mxs_t('info.scriptExecSuccessfully')
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
