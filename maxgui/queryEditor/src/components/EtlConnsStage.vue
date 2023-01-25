<template>
    <v-form ref="form" v-model="isFormValid" class="form-container fill-height">
        <etl-stage-ctr>
            <template v-slot:body>
                <v-row class="fill-height">
                    <v-col cols="12" md="6" class="fill-height pt-0 mt-n1">
                        <etl-src-conn v-model="src" :drivers="odbc_drivers" class="pb-1" />
                    </v-col>
                    <v-col cols="12" md="6" class="fill-height pt-0 mt-n1">
                        <div class="d-flex flex-column fill-height">
                            <div class="d-flex">
                                <etl-dest-conn
                                    v-model="dest"
                                    :allServers="allServers"
                                    :destTargetType="destTargetType"
                                />
                            </div>
                            <etl-logs class="mt-4 etl-logs overflow-y-auto" />
                        </div>
                    </v-col>
                </v-row>
            </template>
            <template v-slot:footer>
                <v-btn
                    small
                    height="36"
                    color="primary"
                    class="font-weight-medium px-7 text-capitalize"
                    rounded
                    depressed
                    :disabled="!isFormValid"
                    :loading="isLoading"
                    @click="next"
                >
                    {{ $mxs_t(hasActiveConns ? 'selectObjsToMigrate' : 'connect') }}
                </v-btn>
            </template>
        </etl-stage-ctr>
    </v-form>
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
import QueryConn from '@queryEditorSrc/store/orm/models/QueryConn'
import EtlStageCtr from '@queryEditorSrc/components/EtlStageCtr.vue'
import EtlSrcConn from '@queryEditorSrc/components/EtlSrcConn.vue'
import EtlDestConn from '@queryEditorSrc/components/EtlDestConn.vue'
import EtlLogs from '@queryEditorSrc/components/EtlLogs.vue'
import { mapActions, mapState, mapMutations } from 'vuex'

export default {
    name: 'etl-conns-stage',
    components: { EtlStageCtr, EtlSrcConn, EtlDestConn, EtlLogs },
    data() {
        return {
            isFormValid: false,
            src: { connection_string: '', type: '' },
            dest: { user: '', password: '', db: '', target: '' },
            isLoading: false,
        }
    },
    computed: {
        ...mapState({
            odbc_drivers: state => state.queryConnsMem.odbc_drivers,
            rc_target_names_map: state => state.queryConnsMem.rc_target_names_map,
            QUERY_CONN_BINDING_TYPES: state => state.mxsWorkspace.config.QUERY_CONN_BINDING_TYPES,
        }),
        destTargetType() {
            return 'servers'
        },
        allServers() {
            return this.rc_target_names_map[this.destTargetType] || []
        },
        activeEtlTask() {
            return EtlTask.getters('getActiveEtlTaskWithRelation')
        },
        activeSrcConn() {
            return EtlTask.getters('getActiveSrcConn')
        },
        activeDestConn() {
            return EtlTask.getters('getActiveDestConn')
        },
        hasActiveConns() {
            return this.$typy(this.activeEtlTask, 'connections').safeArray.length >= 2
        },
    },
    async created() {
        await this.fetchOdbcDrivers()
        await this.fetchRcTargetNames(this.destTargetType)
        await this.validateActiveEtlTaskConns({ silentValidation: true })
    },
    methods: {
        ...mapActions({
            fetchOdbcDrivers: 'queryConnsMem/fetchOdbcDrivers',
            fetchRcTargetNames: 'queryConnsMem/fetchRcTargetNames',
            validateActiveEtlTaskConns: 'etlMem/validateActiveEtlTaskConns',
        }),
        ...mapMutations({
            SET_SNACK_BAR_MESSAGE: 'mxsApp/SET_SNACK_BAR_MESSAGE',
        }),
        /**
         * TODO: handle shown connections open error.
         * Right now it's shown automatically in app snackbar because the requests
         * are called via $queryHttp axios
         */
        async handleOpenConns() {
            const etl_task_id = this.activeEtlTask.id
            this.isLoading = true
            EtlTask.dispatch('pushLog', {
                id: etl_task_id,
                log: {
                    timestamp: new Date().valueOf(),
                    name: this.$mxs_t('info.openingConns'),
                },
            })
            if (!this.activeSrcConn.id)
                await QueryConn.dispatch('openEtlConn', {
                    body: {
                        target: 'odbc',
                        connection_string: this.src.connection_string,
                    },
                    binding_type: this.QUERY_CONN_BINDING_TYPES.ETL_SRC,
                    etl_task_id,
                    meta: { src_type: this.src.type },
                })
            if (!this.activeDestConn.id)
                await QueryConn.dispatch('openEtlConn', {
                    body: this.dest,
                    binding_type: this.QUERY_CONN_BINDING_TYPES.ETL_DEST,
                    etl_task_id,
                    meta: { dest_name: this.dest.target },
                })
            if (this.hasActiveConns) {
                this.SET_SNACK_BAR_MESSAGE({
                    text: [this.$mxs_t('success.connected')],
                    type: 'success',
                })
                await this.$helpers.delay(300) // UX loading animation
            }
            this.isLoading = false
        },
        async next() {
            if (this.hasActiveConns)
                EtlTask.update({
                    where: this.activeEtlTask.id,
                    data(obj) {
                        obj.active_stage_index = obj.active_stage_index + 1
                    },
                })
            else {
                await this.$refs.form.validate()
                if (this.isFormValid) await this.handleOpenConns()
            }
        },
    },
}
</script>

<style lang="scss" scoped>
.form-container {
    overflow-y: auto;
    .etl-logs {
        background-color: #fbfbfb;
        display: flex;
        flex: 1;
        min-height: 250px;
        max-height: 400px;
    }
}
</style>
