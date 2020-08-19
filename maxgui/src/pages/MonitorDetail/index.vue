<template>
    <page-wrapper>
        <v-sheet v-if="!$help.lodash.isEmpty(current_monitor)" class="px-6">
            <page-header :currentMonitor="current_monitor" :onEditSucceeded="fetchMonitor" />
            <overview-header :currentMonitor="current_monitor" />
            <v-row>
                <!-- PARAMETERS TABLE -->
                <v-col cols="6">
                    <details-parameters-collapse
                        :searchKeyword="search_keyword"
                        :resourceId="current_monitor.id"
                        :parameters="current_monitor.attributes.parameters"
                        :moduleParameters="processedModuleParameters"
                        :updateResourceParameters="updateMonitorParameters"
                        :onEditSucceeded="fetchMonitor"
                        :loading="isLoading"
                    />
                </v-col>
                <v-col cols="6">
                    <relationship-table
                        relationshipType="servers"
                        :tableRows="serverStateTableRow"
                        :loading="overlay_type === OVERLAY_TRANSPARENT_LOADING"
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
 * Change Date: 2024-07-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { OVERLAY_TRANSPARENT_LOADING } from 'store/overlayTypes'
import { mapActions, mapState } from 'vuex'
import PageHeader from './PageHeader'
import OverviewHeader from './OverviewHeader'

export default {
    components: {
        PageHeader,
        OverviewHeader,
    },
    data() {
        return {
            OVERLAY_TRANSPARENT_LOADING: OVERLAY_TRANSPARENT_LOADING,
            serverStateTableRow: [],
            processedModuleParameters: [],
            loadingModuleParams: true,
            unmonitoredServers: [],
        }
    },
    computed: {
        ...mapState({
            overlay_type: 'overlay_type',
            search_keyword: 'search_keyword',
            module_parameters: 'module_parameters',
            current_monitor: state => state.monitor.current_monitor,
            all_servers: state => state.server.all_servers,
        }),
        isLoading: function() {
            return this.loadingModuleParams
                ? true
                : this.overlay_type === OVERLAY_TRANSPARENT_LOADING
        },
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
    },

    async created() {
        await this.fetchMonitor()
        const { attributes: { module: moduleName = null } = {} } = this.current_monitor
        if (moduleName) await this.fetchModuleParameters(moduleName)
        this.loadingModuleParams = true
        await this.processModuleParameters()
        await this.serverTableRowProcessing()
    },

    methods: {
        ...mapActions({
            fetchModuleParameters: 'fetchModuleParameters',
            getResourceState: 'getResourceState',
            fetchMonitorById: 'monitor/fetchMonitorById',
            updateMonitorParameters: 'monitor/updateMonitorParameters',
            updateMonitorRelationship: 'monitor/updateMonitorRelationship',
            fetchAllServers: 'server/fetchAllServers',
        }),

        async processModuleParameters() {
            if (this.module_parameters.length) {
                this.processedModuleParameters = this.module_parameters
                await this.$help.delay(150).then(() => (this.loadingModuleParams = false))
            }
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
