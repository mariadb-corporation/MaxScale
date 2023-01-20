<template>
    <etl-stage-ctr>
        <template v-slot:header>
            <div class="etl-migration-script-stage-header">
                <h3 class="etl-stage-title mxs-color-helper text-navigation font-weight-light">
                    {{ $mxs_t('migrationScript') }}
                </h3>
                <p class="mt-4 migration-script-info mxs-color-helper text-deep-ocean">
                    {{ $mxs_t('info.migrationScriptInfo') }}
                </p>
            </div>
        </template>
        <template v-slot:body>
            <etl-migration-tbl
                :headers="tableHeaders"
                :stagingMigrationObjs.sync="stagingMigrationObjs"
            />
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
                    :disabled="!isConfirmed"
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
import EtlMigrationTbl from '@queryEditorSrc/components/EtlMigrationTbl.vue'
import { mapState, mapActions, mapMutations } from 'vuex'

export default {
    name: 'etl-migration-script-stage',
    components: {
        EtlStageCtr,
        EtlMigrationTbl,
    },
    data() {
        return {
            isConfirmed: false,
            stagingMigrationObjs: [],
        }
    },
    computed: {
        ...mapState({
            ETL_STAGE_INDEX: state => state.mxsWorkspace.config.ETL_STAGE_INDEX,
            are_conns_alive: state => state.etlMem.are_conns_alive,
        }),
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
    },
    async created() {
        await this.validateActiveEtlTaskConns()
        await this.getEtlCallRes(this.activeEtlTask.id)
    },

    methods: {
        ...mapMutations({ SET_MIGRATION_OBJS: 'etlMem/SET_MIGRATION_OBJS' }),
        ...mapActions({
            getEtlCallRes: 'etlMem/getEtlCallRes',
            validateActiveEtlTaskConns: 'etlMem/validateActiveEtlTaskConns',
            handleEtlCall: 'etlMem/handleEtlCall',
        }),
        async next() {
            await this.validateActiveEtlTaskConns()
            this.SET_MIGRATION_OBJS(this.stagingMigrationObjs)
            if (this.are_conns_alive) {
                await this.handleEtlCall({
                    id: this.activeEtlTask.id,
                    stageIdx: this.ETL_STAGE_INDEX.DATA_MIGR,
                })
                EtlTask.update({
                    where: this.activeEtlTask.id,
                    data(obj) {
                        obj.active_stage_index = obj.active_stage_index + 1
                    },
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
