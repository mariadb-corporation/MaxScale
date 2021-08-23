<template>
    <page-wrapper>
        <v-sheet v-if="!$help.lodash.isEmpty(current_monitor)" class="px-6">
            <page-header :currentMonitor="current_monitor" :onEditSucceeded="fetchMonitor" />
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
 * Change Date: 2025-08-17
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

        monitorId: function() {
            return this.current_monitor.id
        },
        monitorModule: function() {
            return this.current_monitor.attributes.module
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
            SET_SNACK_BAR_MESSAGE: 'SET_SNACK_BAR_MESSAGE',
            SET_REFRESH_RESOURCE: 'SET_REFRESH_RESOURCE',
        }),
        ...mapActions({
            fetchModuleParameters: 'fetchModuleParameters',
            getResourceState: 'getResourceState',
            fetchMonitorById: 'monitor/fetchMonitorById',
            updateMonitorParameters: 'monitor/updateMonitorParameters',
            updateMonitorRelationship: 'monitor/updateMonitorRelationship',
            switchOver: 'monitor/switchOver',
            fetchAsyncResults: 'monitor/fetchAsyncResults',
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

        async handleSwitchover(masterId) {
            await this.switchOver({
                monitorModule: this.monitorModule,
                monitorId: this.monitorId,
                masterId,
                callback: this.switchOverCb,
            })
        },

        /**
         * This function should be called right after switchover action is called
         * in order to see switchover status message on snackbar.
         * If response meta equals to'switchover completed successfully.',
         * send request to get updated monitor otherwise recursive this function
         * every 2500ms until receive meta string
         */
        async switchOverCb() {
            let res = await this.fetchAsyncResults({
                monitorModule: this.monitorModule,
                monitorId: this.monitorId,
            })
            const { status, data: { meta } = {} } = res
            // response ok
            if (status === 200)
                if (meta === 'switchover completed successfully.')
                    await this.handleSwitchoverDone(meta)
                else await this.handleSwitchoverPending(meta)
        },

        /**
         * @param {String} meta - meta string message
         */
        async handleSwitchoverDone(meta) {
            this.SET_SNACK_BAR_MESSAGE({
                text: [meta],
                type: 'success',
            })
            await this.fetchMonitor()
        },

        /**
         * @param {Object} meta - meta error object
         */
        async handleSwitchoverPending(meta) {
            if (this.shouldPollAsyncResult(meta.errors[0].detail)) {
                this.SET_SNACK_BAR_MESSAGE({
                    text: ['switchover is still running'],
                    type: 'warning',
                })
                // loop fetch until receive success meta
                await this.$help.delay(2500).then(async () => await this.switchOverCb())
            } else {
                const errArr = meta.errors.map(error => error.detail)
                this.SET_SNACK_BAR_MESSAGE({
                    text: errArr,
                    type: 'error',
                })
            }
        },

        shouldPollAsyncResult(msg) {
            let base = 'No manual command results are available, switchover is still'
            return msg === `${base} running.` || msg === `${base} pending.`
        },
    },
}
</script>
