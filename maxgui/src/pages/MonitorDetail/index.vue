<template>
    <page-wrapper>
        <v-sheet v-if="!$helpers.lodash.isEmpty(current_monitor)" class="pl-6">
            <monitor-page-header
                :targetMonitor="current_monitor"
                :successCb="successCb"
                @on-count-done="recurringFetch"
                @is-calling-op="isCallingOp = $event"
            >
                <template v-slot:page-title="{ pageId }">
                    <router-link :to="`/visualization/clusters/${pageId}`" class="rsrc-link">
                        {{ pageId }}
                    </router-link>
                </template>
            </monitor-page-header>
            <overview-header
                :currentMonitor="current_monitor"
                class="pb-3"
                @switch-over="handleSwitchover"
            />
            <v-row>
                <!-- PARAMETERS TABLE -->
                <v-col cols="6">
                    <details-parameters-table
                        :resourceId="current_monitor.id"
                        :parameters="current_monitor.attributes.parameters"
                        :moduleParameters="module_parameters"
                        :updateResourceParameters="updateMonitorParameters"
                        :onEditSucceeded="fetchMonitor"
                        :objType="MXS_OBJ_TYPES.MONITORS"
                    />
                </v-col>
                <v-col cols="6">
                    <v-row>
                        <v-col cols="12">
                            <relationship-table
                                relationshipType="servers"
                                addable
                                removable
                                :tableRows="serverStateTableRow"
                                :getRelationshipData="fetchAllServers"
                                :selectItems="unmonitoredServers"
                                @on-relationship-update="dispatchRelationshipUpdate"
                            />
                        </v-col>
                        <v-col v-if="isColumnStoreCluster && isAdmin" cols="12">
                            <details-readonly-table
                                :title="`${$mxs_t('csStatus')}`"
                                :tableData="curr_cs_status"
                                isTree
                                expandAll
                                :noDataText="cs_no_data_txt || $mxs_t('$vuetify.noDataText')"
                                :isLoadingData="isFirstFetch && is_loading_cs_status"
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
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
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
            isFirstFetch: true,
            isCallingOp: false,
        }
    },
    computed: {
        ...mapState({
            should_refresh_resource: 'should_refresh_resource',
            current_monitor: state => state.monitor.current_monitor,
            module_parameters: 'module_parameters',
            curr_cs_status: state => state.monitor.curr_cs_status,
            is_loading_cs_status: state => state.monitor.is_loading_cs_status,
            cs_no_data_txt: state => state.monitor.cs_no_data_txt,
            all_servers: state => state.server.all_servers,
            MONITOR_OP_TYPES: state => state.app_config.MONITOR_OP_TYPES,
            MXS_OBJ_TYPES: state => state.app_config.MXS_OBJ_TYPES,
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
                if (this.$helpers.lodash.isEmpty(server.relationships.monitors))
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
    },

    methods: {
        ...mapMutations({
            SET_REFRESH_RESOURCE: 'SET_REFRESH_RESOURCE',
        }),
        ...mapActions({
            fetchModuleParameters: 'fetchModuleParameters',
            getResourceData: 'getResourceData',
            fetchMonitorById: 'monitor/fetchMonitorById',
            updateMonitorParameters: 'monitor/updateMonitorParameters',
            updateMonitorRelationship: 'monitor/updateMonitorRelationship',
            manipulateMonitor: 'monitor/manipulateMonitor',
            handleFetchCsStatus: 'monitor/handleFetchCsStatus',
            fetchAllServers: 'server/fetchAllServers',
        }),

        async initialFetch() {
            await this.recurringFetch()
            const { attributes: { module: moduleName = null } = {} } = this.current_monitor
            if (moduleName) await this.fetchModuleParameters(moduleName)
        },

        async successCb() {
            await this.fetchMonitor()
            await this.serverTableRowProcessing()
        },

        async recurringFetch() {
            await this.fetchMonitor()
            await this.serverTableRowProcessing()
            if (!this.isCallingOp)
                await this.handleFetchCsStatus({
                    monitorId: this.monitorId,
                    monitorModule: this.monitorModule,
                    isCsCluster: this.isColumnStoreCluster,
                    monitorState: this.monitorState,
                    successCb: () => (this.isFirstFetch = false),
                    pollingResInterval: 1000,
                })
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
                const { id, type, attributes: { state = null } = {} } = await this.getResourceData({
                    id: server.id,
                    type: 'servers',
                })
                arr.push({ id: id, state: state, type: type })
            }
            this.serverStateTableRow = arr
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
