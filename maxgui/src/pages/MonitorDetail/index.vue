<template>
    <page-wrapper>
        <v-sheet v-if="!$help.lodash.isEmpty(current_monitor)" class="pl-6">
            <monitor-page-header
                :targetMonitor="current_monitor"
                :successCb="initialFetch"
                @on-count-done="initialFetch"
            >
                <template v-slot:page-title="{ pageId }">
                    <router-link :to="`/visualization/clusters/${pageId}`" class="rsrc-link">
                        {{ pageId }}
                    </router-link>
                </template>
            </monitor-page-header>
            <overview-header :currentMonitor="current_monitor" @switch-over="handleSwitchover" />
            <v-row class="my-0">
                <!-- PARAMETERS TABLE -->
                <v-col cols="6">
                    <details-parameters-table
                        :resourceId="current_monitor.id"
                        :parameters="current_monitor.attributes.parameters"
                        :updateResourceParameters="updateMonitorParameters"
                        :onEditSucceeded="fetchMonitor"
                    />
                </v-col>
                <v-col cols="6">
                    <v-row class="my-0 pa-0 ma-0">
                        <v-col cols="12" class="pa-0 ma-0">
                            <relationship-table
                                relationshipType="servers"
                                :tableRows="serverStateTableRow"
                                :getRelationshipData="fetchAllServers"
                                :selectItems="unmonitoredServers"
                                @on-relationship-update="dispatchRelationshipUpdate"
                            />
                        </v-col>
                        <v-col v-if="isColumnStoreCluster && isAdmin" cols="12" class="pa-0 mt-4">
                            <details-readonly-table
                                :title="`${$t('csStatus')}`"
                                :tableData="curr_cs_status"
                                isTree
                                expandAll
                                :noDataText="csStatusNoDataText"
                                :isLoadingData="isFirstFetch && isLoadingCsStatus"
                            />
                        </v-col>
                    </v-row>
                </v-col>
            </v-row>
        </v-sheet>
    </page-wrapper>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-07-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapState, mapMutations, mapGetters } from 'vuex'
import OverviewHeader from './OverviewHeader'

export default {
    components: {
        OverviewHeader,
    },
    data() {
        return {
            serverStateTableRow: [],
            unmonitoredServers: [],
            isLoadingCsStatus: false,
            isFirstFetch: true,
            csStatusNoDataText: this.$t('$vuetify.noDataText'),
        }
    },
    computed: {
        ...mapState({
            should_refresh_resource: 'should_refresh_resource',
            current_monitor: state => state.monitor.current_monitor,
            curr_cs_status: state => state.monitor.curr_cs_status,
            all_servers: state => state.server.all_servers,
            MONITOR_OP_TYPES: state => state.app_config.MONITOR_OP_TYPES,
        }),
        ...mapGetters({ isAdmin: 'user/isAdmin' }),
        monitorId() {
            return this.current_monitor.id
        },
        monitorModule() {
            return this.$typy(this.current_monitor, 'attributes.module').safeString
        },
        monitorState() {
            return this.$typy(this.current_monitor, 'attributes.state').safeString
        },
        isColumnStoreCluster() {
            return Boolean(
                this.$typy(this.current_monitor, 'attributes.parameters.cs_admin_api_key')
                    .safeString
            )
        },
    },
    watch: {
        all_servers() {
            let availableEntities = []
            this.all_servers.forEach(server => {
                if (this.$help.lodash.isEmpty(server.relationships.monitors))
                    availableEntities.push({
                        id: server.id,
                        state: server.attributes.state,
                        type: server.type,
                    })
            })
            this.unmonitoredServers = availableEntities
        },
        should_refresh_resource: async function(val) {
            if (val) {
                this.SET_REFRESH_RESOURCE(false)
                await this.initialFetch()
            }
        },
        // re-fetch when the route changes
        $route: async function() {
            await this.initialFetch()
        },
    },

    async created() {
        await this.initialFetch()
        this.isFirstFetch = false
    },

    methods: {
        ...mapMutations({
            SET_REFRESH_RESOURCE: 'SET_REFRESH_RESOURCE',
            SET_CURR_CS_STATUS: 'monitor/SET_CURR_CS_STATUS',
        }),
        ...mapActions({
            fetchModuleParameters: 'fetchModuleParameters',
            getResourceState: 'getResourceState',
            fetchMonitorById: 'monitor/fetchMonitorById',
            updateMonitorParameters: 'monitor/updateMonitorParameters',
            updateMonitorRelationship: 'monitor/updateMonitorRelationship',
            manipulateMonitor: 'monitor/manipulateMonitor',
            fetchAllServers: 'server/fetchAllServers',
        }),

        async initialFetch() {
            await this.fetchMonitor()
            const { attributes: { module: moduleName = null } = {} } = this.current_monitor
            if (moduleName) await this.fetchModuleParameters(moduleName)
            await this.serverTableRowProcessing()
            if (
                this.isAdmin &&
                this.isColumnStoreCluster &&
                !this.isLoadingCsStatus &&
                this.monitorState !== 'Stopped'
            )
                await this.getColumnStoreStatus()
        },

        async fetchMonitor() {
            await this.fetchMonitorById(this.$route.params.id)
        },

        async serverTableRowProcessing() {
            const {
                relationships: { servers: { data: serversData = [] } = {} } = {},
            } = this.current_monitor
            let arr = []
            for (const server of serversData) {
                const data = await this.getResourceState({
                    resourceId: server.id,
                    resourceType: 'servers',
                    caller: 'monitor-detail-page-getRelationshipData',
                })

                const { id, type, attributes: { state = null } = {} } = data
                arr.push({ id: id, state: state, type: type })
            }
            this.serverStateTableRow = arr
        },

        async getColumnStoreStatus() {
            this.isLoadingCsStatus = true
            await this.manipulateMonitor({
                id: this.monitorId,
                type: this.MONITOR_OP_TYPES.CS_GET_STATUS,
                showSnackbar: false,
                successCb: meta => {
                    this.SET_CURR_CS_STATUS(meta)
                    this.isLoadingCsStatus = false
                },
                asyncCmdErrCb: meta => {
                    this.SET_CURR_CS_STATUS({})
                    this.isLoadingCsStatus = false
                    this.csStatusNoDataText = meta.join(', ')
                },
                opParams: { moduleType: this.monitorModule, params: '' },
            })
        },

        async dispatchRelationshipUpdate({ type, data }) {
            await this.updateMonitorRelationship({
                id: this.current_monitor.id,
                [type]: data,
                callback: this.fetchMonitor,
            })
            await this.serverTableRowProcessing()
        },

        async handleSwitchover(masterId) {
            await this.manipulateMonitor({
                id: this.monitorId,
                type: this.MONITOR_OP_TYPES.SWITCHOVER,
                opParams: {
                    moduleType: this.monitorModule,
                    params: `&${masterId}`,
                },
                successCb: this.fetchMonitor,
            })
        },
    },
}
</script>
