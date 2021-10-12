<template>
    <page-wrapper>
        <v-sheet v-if="!$help.lodash.isEmpty(current_server)" class="px-6">
            <page-header :currentServer="current_server" :onEditSucceeded="dispatchFetchServer" />
            <overview-header
                :currentServer="current_server"
                :getRelationshipData="getRelationshipData"
                @on-relationship-update="dispatchRelationshipUpdate"
            />
            <v-tabs v-model="currentActiveTab" class="tab-navigation-wrapper">
                <v-tab v-for="tab in tabs" :key="tab.name">
                    {{ tab.name }}
                </v-tab>

                <v-tabs-items v-model="currentActiveTab">
                    <v-tab-item class="pt-5">
                        <v-row class="my-0">
                            <v-col class="py-0 my-0" cols="4">
                                <v-row class="my-0 pa-0 ma-0">
                                    <v-col cols="12" class="pa-0 ma-0">
                                        <details-readonly-table
                                            ref="statistics-table"
                                            :title="`${$tc('statistics', 2)}`"
                                            :tableData="current_server_stats"
                                            isTree
                                        />
                                    </v-col>
                                    <v-col cols="12" class="pa-0 mt-4">
                                        <relationship-table
                                            relationshipType="services"
                                            :tableRows="serviceTableRow"
                                            :getRelationshipData="getRelationshipData"
                                            @on-relationship-update="dispatchRelationshipUpdate"
                                        />
                                    </v-col>
                                </v-row>
                            </v-col>
                            <v-col class="py-0 ma-0" cols="8">
                                <details-readonly-table
                                    ref="sessions-table"
                                    :tdBorderLeft="false"
                                    :title="`${$tc('currentSessions', 2)}`"
                                    :titleInfo="sessionsTableRow.length"
                                    :noDataText="$t('noEntity', { entityName: $tc('sessions', 2) })"
                                    :tableData="sessionsTableRow"
                                    :customTableHeaders="sessionsTableHeader"
                                />
                            </v-col>
                        </v-row>
                    </v-tab-item>
                    <!-- Parameters & Diagnostics tab -->
                    <v-tab-item class="pt-5">
                        <v-row>
                            <v-col class="py-0 my-0" cols="6">
                                <details-parameters-table
                                    :resourceId="current_server.id"
                                    :parameters="current_server.attributes.parameters"
                                    usePortOrSocket
                                    :updateResourceParameters="updateServerParameters"
                                    :onEditSucceeded="dispatchFetchServer"
                                />
                            </v-col>
                            <v-col class="py-0 my-0" cols="6">
                                <details-readonly-table
                                    ref="diagnostics-table"
                                    :title="`${$t('monitorDiagnostics')}`"
                                    :tableData="monitorDiagnostics"
                                    expandAll
                                    isTree
                                />
                            </v-col>
                        </v-row>
                    </v-tab-item>
                </v-tabs-items>
            </v-tabs>
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
 * Change Date: 2025-10-11
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapMutations, mapState } from 'vuex'
import PageHeader from './PageHeader'
import OverviewHeader from './OverviewHeader'

export default {
    name: 'server-detail',
    components: {
        PageHeader,
        OverviewHeader,
    },

    data() {
        return {
            currentActiveTab: null,
            tabs: [
                { name: `${this.$tc('statistics', 2)} & ${this.$tc('sessions', 2)}` },
                { name: `${this.$tc('parameters', 2)} & ${this.$tc('diagnostics', 2)}` },
            ],
            serviceTableRow: [],
            sessionsTableHeader: [
                { text: 'ID', value: 'id' },
                { text: 'Client', value: 'user' },
                { text: 'Connected', value: 'connected' },
                { text: 'IDLE (s)', value: 'idle' },
            ],
            isLoopFetch: true,
        }
    },
    computed: {
        ...mapState({
            should_refresh_resource: 'should_refresh_resource',
            search_keyword: 'search_keyword',
            current_server: state => state.server.current_server,
            current_server_stats: state => state.server.current_server_stats,
            monitor_diagnostics: state => state.monitor.monitor_diagnostics,
            all_sessions: state => state.session.all_sessions,
        }),

        monitorDiagnostics: function() {
            const {
                attributes: { monitor_diagnostics: { server_info = [] } = {} } = {},
            } = this.monitor_diagnostics
            return server_info.find(server => server.name === this.$route.params.id) || {}
        },
        sessionsTableRow: function() {
            let tableRows = []
            this.all_sessions.forEach(session => {
                const {
                    id,
                    attributes: { idle, connected, user, remote, connections },
                } = session

                let connectionOfThisServer = connections.find(
                    connection => connection.server === this.current_server.id
                )

                if (connectionOfThisServer) {
                    tableRows.push({
                        id: id,
                        user: `${user}@${remote}`,
                        connected: this.$help.dateFormat({ value: connected }),
                        idle: idle,
                    })
                }
            })
            return tableRows
        },
    },
    watch: {
        should_refresh_resource: async function(val) {
            if (val) {
                this.SET_REFRESH_RESOURCE(false)
                await this.initialFetch()
            }
        },
        currentActiveTab: async function(val, oldVal) {
            switch (val) {
                case 0:
                    // ignore when component is first created
                    if (oldVal !== null) {
                        await this.initialFetch()
                    }
                    break
                // when active tab is Parameters & Diagnostics
                case 1:
                    await this.fetchModuleParameters('servers')
                    break
            }
        },
        isLoopFetch: async function(val) {
            if (val) await this.loopFetch()
        },
        // re-fetch when the route changes
        $route: async function() {
            await this.initialFetch()
            if (this.currentActiveTab === 1) await this.fetchModuleParameters('servers')
        },
    },
    async created() {
        await this.initialFetch()
    },
    beforeDestroy() {
        this.isLoopFetch = false
    },
    methods: {
        ...mapMutations({
            SET_REFRESH_RESOURCE: 'SET_REFRESH_RESOURCE',
            SET_MONITOR_DIAGNOSTICS: 'monitor/SET_MONITOR_DIAGNOSTICS',
        }),
        ...mapActions({
            getResourceState: 'getResourceState',
            fetchModuleParameters: 'fetchModuleParameters',
            fetchServerById: 'server/fetchServerById',
            fetchServerStatsById: 'server/fetchServerStatsById',
            updateServerRelationship: 'server/updateServerRelationship',
            updateServerParameters: 'server/updateServerParameters',
            fetchMonitorDiagnosticsById: 'monitor/fetchMonitorDiagnosticsById',
            fetchAllSessions: 'session/fetchAllSessions',
        }),
        async loopFetch() {
            while (this.isLoopFetch) {
                /*
                    Sessions, Stats and Monitor diagnostics should be
                    fetched together as their data point changes over time
                */
                await Promise.all([
                    this.fetchAllSessions(),
                    this.fetchServerStatsById(this.$route.params.id),
                    this.fetchMonitorDiagnostics(),
                    this.$help.delay(10000),
                ])
            }
        },
        async initialFetch() {
            await this.dispatchFetchServer()
            await this.serviceTableRowProcessing()
            // loopFetch should go last
            await this.loopFetch()
        },
        async fetchMonitorDiagnostics() {
            const { relationships: { monitors = {} } = {} } = this.current_server
            if (monitors.data) {
                const monitorId = monitors.data[0].id
                await this.fetchMonitorDiagnosticsById(monitorId)
            } else {
                this.SET_MONITOR_DIAGNOSTICS({})
            }
        },
        // reuse functions for fetch loop or after finish editing
        async dispatchFetchServer() {
            await this.fetchServerById(this.$route.params.id)
        },

        async serviceTableRowProcessing() {
            const {
                relationships: { services: { data: servicesData = [] } = {} } = {},
            } = this.current_server
            let arr = []
            servicesData.forEach(async service => {
                const data = await this.getRelationshipData('services', service.id)
                const { id, type, attributes: { state = null } = {} } = data
                arr.push({ id, state, type })
            })
            this.serviceTableRow = arr
        },

        /**
         * This function fetch all resource state if id is not provided
         * otherwise it fetch a resource state.
         * Even filter doesn't have state, the request still success
         * @param {String} type type of resource: services, monitors
         * @param {String} id name of the resource (optional)
         * @return {Array} Resource state data
         */
        async getRelationshipData(type, id) {
            const data = await this.getResourceState({
                resourceId: id,
                resourceType: type,
                caller: 'server-detail-page-getRelationshipData',
            })
            return data
        },
        // actions to vuex
        async dispatchRelationshipUpdate({ type, data }) {
            await this.updateServerRelationship({
                id: this.current_server.id,
                type,
                [type]: data,
                callback: this.dispatchFetchServer,
            })
            switch (type) {
                case 'monitors':
                    await this.fetchMonitorDiagnostics()
                    break
                case 'services':
                    await this.serviceTableRowProcessing()
                    break
            }
        },
    },
}
</script>
