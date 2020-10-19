<template>
    <page-wrapper>
        <v-sheet v-if="!$help.lodash.isEmpty(current_monitor)" class="px-6">
            <page-header :currentMonitor="current_monitor" :onEditSucceeded="fetchMonitor" />
            <overview-header :currentMonitor="current_monitor" />
            <v-row>
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
                    <relationship-table
                        relationshipType="servers"
                        :tableRows="serverStateTableRow"
                        :getRelationshipData="fetchAllServers"
                        :selectItems="unmonitoredServers"
                        @on-relationship-update="dispatchRelationshipUpdate"
                    />
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
 * Change Date: 2024-10-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapState, mapMutations } from 'vuex'
import PageHeader from './PageHeader'
import OverviewHeader from './OverviewHeader'

export default {
    components: {
        PageHeader,
        OverviewHeader,
    },
    data() {
        return {
            serverStateTableRow: [],
            unmonitoredServers: [],
        }
    },
    computed: {
        ...mapState({
            should_refresh_resource: 'should_refresh_resource',
            current_monitor: state => state.monitor.current_monitor,
            all_servers: state => state.server.all_servers,
        }),
    },
    watch: {
        all_servers: function() {
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
    },

    methods: {
        ...mapMutations({
            SET_REFRESH_RESOURCE: 'SET_REFRESH_RESOURCE',
        }),
        ...mapActions({
            fetchModuleParameters: 'fetchModuleParameters',
            getResourceState: 'getResourceState',
            fetchMonitorById: 'monitor/fetchMonitorById',
            updateMonitorParameters: 'monitor/updateMonitorParameters',
            updateMonitorRelationship: 'monitor/updateMonitorRelationship',
            fetchAllServers: 'server/fetchAllServers',
        }),

        async initialFetch() {
            await this.fetchMonitor()
            const { attributes: { module: moduleName = null } = {} } = this.current_monitor
            if (moduleName) await this.fetchModuleParameters(moduleName)
            await this.serverTableRowProcessing()
        },

        async fetchMonitor() {
            await this.fetchMonitorById(this.$route.params.id)
        },

        async serverTableRowProcessing() {
            const {
                relationships: { servers: { data: serversData = [] } = {} } = {},
            } = this.current_monitor

            let arr = []
            serversData.forEach(async server => {
                const data = await this.getResourceState({
                    resourceId: server.id,
                    resourceType: 'servers',
                    caller: 'monitor-detail-page-getRelationshipData',
                })

                const { id, type, attributes: { state = null } = {} } = data
                arr.push({ id: id, state: state, type: type })
            })
            this.serverStateTableRow = arr
        },

        // actions to vuex
        async dispatchRelationshipUpdate({ type, data }) {
            await this.updateMonitorRelationship({
                id: this.current_monitor.id,
                [type]: data,
                callback: this.fetchMonitor,
            })
            await this.serverTableRowProcessing()
        },
    },
}
</script>
