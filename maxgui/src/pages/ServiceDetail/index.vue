<template>
    <page-wrapper>
        <v-sheet v-if="!$help.lodash.isEmpty(current_service)" class="px-6">
            <page-header :currentService="current_service" :onEditSucceeded="fetchService" />
            <!--
                @update-chart is emitted every 10s
            -->
            <overview-header
                :currentService="current_service"
                :serviceConnectionsDatasets="service_connections_datasets"
                :serviceConnectionInfo="service_connection_info"
                @update-chart="fetchConnectionsAndSession"
            />

            <v-tabs v-model="currentActiveTab" class="tab-navigation-wrapper">
                <v-tab v-for="tab in tabs" :key="tab.name">
                    {{ tab.name }}
                </v-tab>

                <v-tabs-items v-model="currentActiveTab">
                    <v-tab-item class="pt-5">
                        <v-row>
                            <v-col class="py-0 my-0" cols="4">
                                <v-row class="pa-0 ma-0">
                                    <v-col cols="12" class="pa-0 ma-0">
                                        <relationship-table
                                            ref="servers-relationship-table"
                                            relationshipType="servers"
                                            :tableRows="serversTableRows"
                                            :getRelationshipData="getRelationshipData"
                                            @on-relationship-update="dispatchRelationshipUpdate"
                                        />
                                    </v-col>
                                    <v-col cols="12" class="pa-0 mt-4">
                                        <relationship-table
                                            ref="filters-relationship-table"
                                            relationshipType="filters"
                                            :tableRows="filtersTableRows"
                                            :getRelationshipData="getRelationshipData"
                                            @on-relationship-update="dispatchRelationshipUpdate"
                                        />
                                    </v-col>
                                    <v-col cols="12" class="pa-0 mt-4">
                                        <relationship-table
                                            ref="listeners-relationship-table"
                                            relationshipType="listeners"
                                            :tableRows="listenersTableRows"
                                            readOnly
                                            @open-listener-form-dialog="
                                                SET_FORM_TYPE(FORM_LISTENER)
                                            "
                                        />
                                    </v-col>
                                </v-row>
                            </v-col>
                            <v-col class="py-0 ma-0" cols="8">
                                <details-readonly-table
                                    ref="sessions-table"
                                    tableClass="data-table-full--max-width-columns"
                                    :tdBorderLeft="false"
                                    :title="`${$tc('currentSessions', 2)}`"
                                    :titleInfo="sessionsTableRows.length"
                                    :noDataText="$t('noEntity', { entityName: $tc('sessions', 2) })"
                                    :tableData="sessionsTableRows"
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
                                    :resourceId="current_service.id"
                                    :parameters="current_service.attributes.parameters"
                                    :updateResourceParameters="updateServiceParameters"
                                    :onEditSucceeded="fetchService"
                                />
                            </v-col>
                            <v-col class="py-0 my-0" cols="6">
                                <details-readonly-table
                                    ref="diagnostics-table"
                                    :title="`${$t('routerDiagnostics')}`"
                                    :tableData="routerDiagnostics"
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
 * Change Date: 2025-03-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { FORM_LISTENER } from 'store/formTypes'
import { mapActions, mapMutations, mapState } from 'vuex'
import OverviewHeader from './OverviewHeader'
import PageHeader from './PageHeader'

export default {
    name: 'service-detail',
    components: {
        PageHeader,
        OverviewHeader,
    },
    data() {
        return {
            FORM_LISTENER: FORM_LISTENER,
            currentActiveTab: null,
            tabs: [
                { name: `${this.$tc('servers', 2)} & ${this.$tc('sessions', 2)}` },
                { name: `${this.$tc('parameters', 2)} & ${this.$tc('diagnostics', 2)}` },
            ],
            serversTableRows: [],
            listenersTableRows: [],
            filtersTableRows: [],
            sessionsTableHeader: [
                { text: 'ID', value: 'id' },
                { text: 'Client', value: 'user' },
                { text: 'Connected', value: 'connected' },
                { text: 'IDLE (s)', value: 'idle' },
            ],
        }
    },
    computed: {
        ...mapState({
            should_refresh_resource: 'should_refresh_resource',
            current_service: state => state.service.current_service,
            service_connections_datasets: state => state.service.service_connections_datasets,
            service_connection_info: state => state.service.service_connection_info,
            sessions_by_service: state => state.session.sessions_by_service,
        }),

        routerDiagnostics: function() {
            const { attributes: { router_diagnostics = {} } = {} } = this.current_service
            return router_diagnostics
        },
        routerModule: function() {
            return this.current_service.attributes.router
        },
        sessionsTableRows: function() {
            return this.sessions_by_service.map(
                ({ id, attributes: { idle, connected, user, remote } }) => ({
                    id,
                    user: `${user}@${remote}`,
                    connected: this.$help.dateFormat({ value: connected }),
                    idle,
                })
            )
        },
    },
    watch: {
        should_refresh_resource: async function(val) {
            if (val) {
                this.SET_REFRESH_RESOURCE(false)
                await this.initialFetch()
            }
        },
        currentActiveTab: async function(val) {
            // when active tab is Parameters & Diagnostics
            if (val === 1) await this.fetchModuleParameters(this.routerModule)
        },
        // re-fetch when the route changes
        $route: async function() {
            await this.initialFetch()
            if (this.currentActiveTab === 1) await this.fetchModuleParameters(this.routerModule)
        },
    },
    async created() {
        await this.initialFetch()
    },
    methods: {
        ...mapActions({
            getResourceState: 'getResourceState',
            fetchModuleParameters: 'fetchModuleParameters',
            fetchServiceById: 'service/fetchServiceById',
            fetchServiceConnections: 'service/fetchServiceConnections',
            genServiceConnectionsDataSets: 'service/genDataSets',
            updateServiceRelationship: 'service/updateServiceRelationship',
            updateServiceParameters: 'service/updateServiceParameters',
            fetchSessionsFilterByService: 'session/fetchSessionsFilterByService',
            fetchAllFilters: 'filter/fetchAllFilters',
        }),
        ...mapMutations({
            SET_FORM_TYPE: 'SET_FORM_TYPE',
            SET_REFRESH_RESOURCE: 'SET_REFRESH_RESOURCE',
        }),

        async initialFetch() {
            await this.fetchService()
            await this.genServiceConnectionsDataSets()
            await this.fetchConnectionsAndSession()
            await Promise.all([
                this.processingRelationshipTable('servers'),
                this.processingRelationshipTable('filters'),
                this.processingRelationshipTable('listeners'),
            ])
        },
        // reuse functions for fetch loop or after finish editing
        async fetchService() {
            await this.fetchServiceById(this.$route.params.id)
        },

        async fetchConnectionsAndSession() {
            const serviceId = this.$route.params.id
            // fetching connections chart info should be at the same time with fetchSessionsFilterByService
            await Promise.all([
                this.fetchServiceConnections(serviceId),
                this.fetchSessionsFilterByService(serviceId),
            ])
        },

        /**
         * This function get relationship data based on relationship type i.e. servers, listeners.
         * It loops through id array to send sequential requests to get relationship state
         * @param {String} relationshipType type of resource. e.g. servers, listeners
         */
        async processingRelationshipTable(relationshipType) {
            const {
                relationships: {
                    [`${relationshipType}`]: { data: relationshipData = [] } = {},
                } = {},
            } = this.current_service

            let ids = relationshipData.length ? relationshipData.map(item => `${item.id}`) : []
            let arr = []
            ids.forEach(async id => {
                let data = await this.getRelationshipData(relationshipType, id)
                const { id: relationshipId, type, attributes: { state = null } = {} } = data
                let row = { id: relationshipId, type: type }
                if (relationshipType !== 'filters') row.state = state
                arr.push(row)
            })

            this[`${relationshipType}TableRows`] = arr
        },

        /**
         * This function fetch all resource state if id is not provided
         * otherwise it fetch a resource state.
         * Even filter doesn't have state, the request still success
         * @param {String} type type of resource: servers, listeners, filters
         * @param {String} id name of the resource (optional)
         * @return {Array} Resource state data
         */
        async getRelationshipData(type, id) {
            const data = await this.getResourceState({
                resourceId: id,
                resourceType: type,
                caller: 'service-detail-page-getRelationshipData',
            })

            return data
        },

        // actions to vuex
        async dispatchRelationshipUpdate({ type, data, isFilterDrag }) {
            await this.updateServiceRelationship({
                id: this.current_service.id,
                type: type,
                [type]: data,
                callback: this.fetchService,
            })
            switch (type) {
                case 'filters':
                    if (!isFilterDrag) await this.processingRelationshipTable('filters')
                    break
                case 'servers':
                    await this.processingRelationshipTable('servers')
                    break
            }
        },
    },
}
</script>
