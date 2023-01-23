<template>
    <etl-stage-ctr>
        <template v-slot:header>
            <div class="etl-migration-script-stage-header">
                <h3 class="etl-stage-title mxs-color-helper text-navigation font-weight-light">
                    {{ $mxs_t('migrationProgress') }}
                </h3>
                <div class="mt-4 d-flex align-center" :style="{ height: '30px' }">
                    <etl-status-icon :status="activeEtlTask.status" :isRunning="isRunning" />
                    <span class="mxs-color-helper text-navigation">
                        {{ $mxs_t(isRunning ? 'running' : 'complete') }}
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
                :headers="tableHeaders"
                :stagingMigrationObjs.sync="stagingMigrationObjs"
                :custom-sort="customSort"
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
            <!-- TODO: Add output messages -->
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
import { mapActions, mapState } from 'vuex'

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
        }
    },
    computed: {
        ...mapState({
            ETL_STATUS: state => state.mxsWorkspace.config.ETL_STATUS,
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
    },

    async created() {
        await this.validateActiveEtlTaskConns()
        await this.getEtlCallRes(this.activeEtlTask.id)
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
