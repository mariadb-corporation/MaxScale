<template>
    <v-form ref="form" v-model="isFormValid" class="form-container fill-height">
        <etl-stage-ctr>
            <template v-slot:body>
                <v-row class="fill-height">
                    <v-col cols="12" md="6" class="fill-height pt-0 mt-n1">
                        <odbc-form v-model="src" :drivers="odbc_drivers" class="pb-1">
                            <template v-slot:prepend>
                                <v-col cols="12" class="pa-1">
                                    <h3
                                        class="etl-stage-title mxs-color-helper text-navigation font-weight-light"
                                    >
                                        {{ $mxs_t('source') }}
                                    </h3>
                                </v-col>
                            </template>
                        </odbc-form>
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
                            <etl-logs :task="task" class="mt-4 etl-logs overflow-y-auto" />
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
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-08-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import EtlTask from '@wsModels/EtlTask'
import QueryConn from '@wsModels/QueryConn'
import EtlStageCtr from '@wkeComps/DataMigration/EtlStageCtr.vue'
import OdbcForm from '@wkeComps/OdbcForm.vue'
import EtlDestConn from '@wkeComps/DataMigration/EtlDestConn.vue'
import EtlLogs from '@wkeComps/DataMigration/EtlLogs.vue'
import { mapActions, mapState, mapMutations } from 'vuex'

export default {
    name: 'etl-conns-stage',
    components: { EtlStageCtr, OdbcForm, EtlDestConn, EtlLogs },
    props: { task: { type: Object, required: true } },
    data() {
        return {
            isFormValid: false,
            src: {},
            dest: {},
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
        activeSrcConn() {
            return QueryConn.getters('getActiveSrcConn')
        },
        activeDestConn() {
            return QueryConn.getters('getActiveDestConn')
        },
        hasActiveConns() {
            return QueryConn.getters('getActiveEtlConns').length >= 2
        },
    },
    async created() {
        await this.fetchOdbcDrivers()
        await this.fetchRcTargetNames(this.destTargetType)
    },
    methods: {
        ...mapActions({
            fetchOdbcDrivers: 'queryConnsMem/fetchOdbcDrivers',
            fetchRcTargetNames: 'queryConnsMem/fetchRcTargetNames',
        }),
        ...mapMutations({
            SET_SNACK_BAR_MESSAGE: 'mxsApp/SET_SNACK_BAR_MESSAGE',
        }),
        async handleOpenConns() {
            const etl_task_id = this.task.id
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
                        timeout: this.src.timeout,
                    },
                    binding_type: this.QUERY_CONN_BINDING_TYPES.ETL_SRC,
                    etl_task_id,
                    taskMeta: { src_type: this.src.type },
                    connMeta: { name: this.src.type },
                })
            if (!this.activeDestConn.id)
                await QueryConn.dispatch('openEtlConn', {
                    body: this.dest,
                    binding_type: this.QUERY_CONN_BINDING_TYPES.ETL_DEST,
                    etl_task_id,
                    taskMeta: { dest_name: this.dest.target },
                    connMeta: { name: this.dest.target },
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
            if (this.hasActiveConns) {
                EtlTask.update({
                    where: this.task.id,
                    data(obj) {
                        obj.active_stage_index = obj.active_stage_index + 1
                    },
                })
                await EtlTask.dispatch('fetchSrcSchemas')
            } else {
                await this.$refs.form.validate()
                if (this.isFormValid) await this.handleOpenConns()
            }
        },
    },
}
</script>

<style lang="scss">
.form-container {
    overflow-y: auto;
    .etl-logs {
        display: flex;
        flex: 1;
        min-height: 250px;
        max-height: 400px;
        .log-container {
            background-color: #fbfbfb !important;
        }
    }
}
</style>
