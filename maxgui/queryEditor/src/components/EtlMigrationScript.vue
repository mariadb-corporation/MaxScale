<template>
    <etl-stage-ctr>
        <template v-slot:header>
            <h3 class="etl-stage-title mxs-color-helper text-navigation font-weight-light">
                {{ $mxs_t('migrationScript') }}
            </h3>
        </template>
        <template v-slot:body>
            <v-col cols="12" class="fill-height">
                <!--  TODO: Add info about tables script are being executed parallelly -->
                <v-progress-linear v-if="isLoading" indeterminate color="primary" />
                <sql-editor
                    v-else
                    v-model="migrationScript"
                    class="script-container  pa-4 mxs-color-helper all-border-separator"
                />
            </v-col>
        </template>
        <template v-slot:footer>
            <v-btn
                small
                height="36"
                color="primary"
                class="mt-auto font-weight-medium px-7 text-capitalize"
                rounded
                depressed
                @click="next"
            >
                {{ $mxs_t('startMigration') }}
            </v-btn>
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
    height: 100%;
}
</style>
