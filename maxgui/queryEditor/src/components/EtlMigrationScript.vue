<template>
    <etl-stage-ctr>
        <template v-slot:header>
            <div class="etl-migration-script-header">
                <h3 class="etl-stage-title mxs-color-helper text-navigation font-weight-light">
                    {{ $mxs_t('migrationScript') }}
                </h3>
                <p class="my-2 migration-method-info mxs-color-helper text-deep-ocean">
                    {{ $mxs_t('info.migrationMethod') }}
                </p>
            </div>
        </template>
        <template v-slot:body>
            <v-col cols="12" class="fill-height">
                <v-progress-linear v-if="isLoading" indeterminate color="primary" />
                <sql-editor
                    v-else
                    v-model="migrationScript"
                    class="script-container fill-height pa-4 mxs-color-helper all-border-separator"
                />
            </v-col>
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
                        <v-tooltip
                            top
                            transition="slide-y-transition"
                            content-class="shadow-drop mxs-color-helper white text-navigation py-1 px-4"
                        >
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
import SqlEditor from './SqlEditor'
import { mapState, mapActions } from 'vuex'

export default {
    name: 'etl-migration-script',
    components: {
        EtlStageCtr,
        'sql-editor': SqlEditor,
    },
    data() {
        return {
            isLoading: true,
            isConfirmed: false,
        }
    },
    computed: {
        ...mapState({
            ETL_STAGE_INDEX: state => state.mxsWorkspace.config.ETL_STAGE_INDEX,
            QUERY_CONN_BINDING_TYPES: state => state.mxsWorkspace.config.QUERY_CONN_BINDING_TYPES,
            are_conns_alive: state => state.etlMem.are_conns_alive,
        }),
        activeEtlTask() {
            return EtlTask.getters('getActiveEtlTaskWithRelation')
        },
        asyncQueryId() {
            return this.$typy(this.activeEtlTask, 'meta.async_query_id').safeString
        },
        migrationScript: {
            get() {
                return this.$typy(this.activeEtlTask, 'meta.sql_script').safeString
            },
            set(v) {
                EtlTask.update({
                    where: this.activeEtlTask.id,
                    data(obj) {
                        obj.meta.sql_script = v
                    },
                })
            },
        },
    },
    watch: {
        // Polling result
        asyncQueryId: {
            immediate: true,
            async handler(v) {
                this.isLoading = Boolean(v)
                if (v)
                    await this.$helpers
                        .delay(2000)
                        .then(async () => await this.getPrepareEtlRes(this.activeEtlTask.id))
            },
        },
    },
    created() {
        this.validateEtlTaskConns()
    },
    methods: {
        ...mapActions({
            validateEtlTaskConns: 'etlMem/validateEtlTaskConns',
            getPrepareEtlRes: 'etlMem/getPrepareEtlRes',
        }),
        next() {
            EtlTask.update({
                where: this.activeEtlTask.id,
                data(obj) {
                    obj.active_stage_index = obj.active_stage_index + 1
                },
            })
        },
    },
}
</script>
<style lang="scss" scoped>
.script-container {
    border-radius: 4px;
}
.confirm-label,
.migration-method-info {
    font-size: 14px;
}
</style>
