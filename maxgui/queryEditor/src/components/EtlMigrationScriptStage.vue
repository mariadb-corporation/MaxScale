<template>
    <etl-stage-ctr>
        <template v-slot:header>
            <div class="etl-migration-script-stage-header">
                <h3 class="etl-stage-title mxs-color-helper text-navigation font-weight-light">
                    {{ $mxs_t('migrationScript') }}
                </h3>
                <div class="d-flex align-center">
                    <etl-status-icon v-if="scriptErr" :status="ETL_STATUS.ERROR" />
                    <p class="mt-4 migration-script-info mxs-color-helper text-deep-ocean">
                        {{
                            scriptErr
                                ? $mxs_t('errors.failedToPrepareMigrationScript')
                                : $mxs_t('info.migrationScriptInfo')
                        }}
                    </p>
                </div>
            </div>
        </template>
        <template v-slot:body>
            <v-progress-linear
                v-if="isLoading"
                indeterminate
                color="primary"
                class="align-self-start"
            />
            <template v-else>
                <etl-logs v-if="scriptErr" class="fill-height" />
                <etl-tbl-script
                    v-else
                    :data="getMigrationPrepareScript"
                    :headers="tableHeaders"
                    @get-staging-data="stagingScript = $event"
                />
            </template>
        </template>
        <template v-slot:footer>
            <div class="btn-ctr">
                <v-checkbox
                    v-model="isConfirmed"
                    color="primary"
                    class="mb-4 v-checkbox--mariadb"
                    hide-details
                >
                    <template v-slot:label>
                        <v-tooltip top transition="slide-y-transition" max-width="340">
                            <template v-slot:activator="{ on }">
                                <div class="d-flex align-center" v-on="on">
                                    <label
                                        class="v-label ml-1 mxs-color-helper text-deep-ocean confirm-label"
                                    >
                                        {{ $mxs_t('etlConfirmMigration') }}
                                    </label>
                                    <v-icon
                                        class="ml-1 material-icons-outlined pointer"
                                        size="16"
                                        color="warning"
                                    >
                                        $vuetify.icons.mxs_statusWarning
                                    </v-icon>
                                </div>
                            </template>
                            <span>{{ $mxs_t('info.etlConfirm') }}</span>
                        </v-tooltip>
                    </template>
                </v-checkbox>
                <v-btn
                    small
                    height="36"
                    color="primary"
                    class="mt-auto font-weight-medium px-7 text-capitalize"
                    rounded
                    depressed
                    :disabled="Boolean(scriptErr) || !isConfirmed"
                    @click="next"
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
import EtlTblScript from '@queryEditorSrc/components/EtlTblScript.vue'
import EtlLogs from '@queryEditorSrc/components/EtlLogs.vue'
import EtlStatusIcon from '@queryEditorSrc/components/EtlStatusIcon.vue'
import { mapState, mapActions, mapMutations, mapGetters } from 'vuex'

export default {
    name: 'etl-migration-script-stage',
    components: {
        EtlStageCtr,
        EtlTblScript,
        EtlLogs,
        EtlStatusIcon,
    },
    data() {
        return {
            isConfirmed: false,
            stagingScript: [],
        }
    },
    computed: {
        ...mapState({
            ETL_STAGE_INDEX: state => state.mxsWorkspace.config.ETL_STAGE_INDEX,
            ETL_STATUS: state => state.mxsWorkspace.config.ETL_STATUS,
            are_conns_alive: state => state.etlMem.are_conns_alive,
            etl_prepare_res: state => state.etlMem.etl_prepare_res,
        }),
        ...mapGetters({ getMigrationPrepareScript: 'etlMem/getMigrationPrepareScript' }),
        activeEtlTask() {
            return EtlTask.getters('getActiveEtlTaskWithRelation')
        },
        // Table data
        tableHeaders() {
            return [
                { text: 'SCHEMA', value: 'schema' },
                { text: 'TABLE', value: 'table' },
            ]
        },
        queryId() {
            return this.$typy(this.activeEtlTask, 'meta.async_query_id').safeString
        },
        isActive() {
            return this.activeEtlTask.active_stage_index === this.ETL_STAGE_INDEX.MIGR_SCRIPT
        },
        scriptErr() {
            return this.$typy(this.etl_prepare_res, 'error').safeString
        },
        isLoading() {
            return this.$typy(this.activeEtlTask, 'meta.is_loading').safeBoolean
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
        ...mapMutations({ SET_ETL_PREPARE_RES: 'etlMem/SET_ETL_PREPARE_RES' }),
        ...mapActions({
            getEtlCallRes: 'etlMem/getEtlCallRes',
            validateActiveEtlTaskConns: 'etlMem/validateActiveEtlTaskConns',
            handleEtlCall: 'etlMem/handleEtlCall',
        }),
        async next() {
            await this.validateActiveEtlTaskConns()
            this.SET_ETL_PREPARE_RES({ ...this.etl_prepare_res, tables: this.stagingScript })
            if (this.are_conns_alive) {
                EtlTask.update({
                    where: this.activeEtlTask.id,
                    data(obj) {
                        obj.active_stage_index = obj.active_stage_index + 1
                    },
                })
                await this.handleEtlCall({
                    id: this.activeEtlTask.id,
                    tables: this.getMigrationPrepareScript,
                })
            }
        },
    },
}
</script>
<style lang="scss" scoped>
.confirm-label,
.migration-script-info {
    font-size: 14px;
}
</style>
