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
                @update-chart="fetchConnSessDiag"
            />

            <v-tabs v-model="currentActiveTab" class="tab-navigation-wrapper">
                <v-tab v-for="tab in tabs" :key="tab.name">
                    {{ tab.name }}
                </v-tab>

                <v-tabs-items v-model="currentActiveTab">
                    <!-- Parameters & relationships tab -->
                    <v-tab-item class="pt-5">
                        <v-row class="my-0">
                            <v-col class="py-0 ma-0" cols="8">
                                <details-parameters-table
                                    :resourceId="current_service.id"
                                    :parameters="current_service.attributes.parameters"
                                    :updateResourceParameters="updateServiceParameters"
                                    :onEditSucceeded="fetchService"
                                />
                            </v-col>
                            <v-col class="py-0 my-0" cols="4">
                                <v-row class="my-0 pa-0 ma-0">
                                    <v-col cols="12" class="pa-0 ma-0">
                                        <routing-target-table
                                            :routerId="current_service.id"
                                            :tableRows="routingTargetsTableRows"
                                            @on-relationship-update="dispatchRelationshipUpdate"
                                        />
                                    </v-col>
                                    <v-col cols="12" class="pa-0 mt-4">
                                        <relationship-table
                                            ref="filters-relationship-table"
                                            relationshipType="filters"
                                            addable
                                            removable
                                            :tableRows="filtersTableRows"
                                            :getRelationshipData="getRelationshipData"
                                            @on-relationship-update="dispatchRelationshipUpdate"
                                        />
                                    </v-col>
                                    <v-col cols="12" class="pa-0 mt-4">
                                        <relationship-table
                                            ref="listeners-relationship-table"
                                            relationshipType="listeners"
                                            addable
                                            :tableRows="listenersTableRows"
                                            @open-listener-form-dialog="
                                                SET_FORM_TYPE(FORM_LISTENER)
                                            "
                                        />
                                    </v-col>
                                </v-row>
                            </v-col>
                        </v-row>
                    </v-tab-item>
                    <!-- Sessions & Diagnostics tab -->
                    <v-tab-item class="pt-5">
                        <v-row>
                            <v-col class="py-0 my-0" cols="6">
                                <details-readonly-table
                                    ref="diagnostics-table"
                                    :title="`${$t('routerDiagnostics')}`"
                                    :tableData="routerDiagnostics"
                                    expandAll
                                    showAll
                                    isTree
                                />
                            </v-col>
                            <v-col class="py-0 my-0" cols="6">
                                <sessions-table
                                    collapsible
                                    delayLoading
                                    :headers="sessionsTableHeader"
                                    :items="sessionsTableRows"
                                    :server-items-length="getTotalFilteredSessions"
                                    @get-data-from-api="fetchSessionsWithFilter(filterSessionParam)"
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
import { FORM_LISTENER } from 'store/formTypes'
import { mapActions, mapMutations, mapState, mapGetters } from 'vuex'
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
                { name: `${this.$tc('parameters', 2)} & ${this.$tc('relationships', 2)}` },
                { name: `${this.$tc('sessions', 2)} & ${this.$tc('diagnostics', 2)}` },
            ],
            routingTargetsTableRows: [],
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
            current_service_diagnostics: state => state.service.current_service_diagnostics,
            service_connections_datasets: state => state.service.service_connections_datasets,
            service_connection_info: state => state.service.service_connection_info,
            filtered_sessions: state => state.session.filtered_sessions,
            ROUTING_TARGET_RELATIONSHIP_TYPES: state =>
                state.app_config.ROUTING_TARGET_RELATIONSHIP_TYPES,
        }),
        ...mapGetters({
            getTotalFilteredSessions: 'session/getTotalFilteredSessions',
            getFilterParamByServiceId: 'session/getFilterParamByServiceId',
        }),
        routerDiagnostics: function() {
            const {
                attributes: { router_diagnostics = {} } = {},
            } = this.current_service_diagnostics
            return router_diagnostics
        },
        routerModule: function() {
            return this.current_service.attributes.router
        },
        sessionsTableRows: function() {
            return this.filtered_sessions.map(
                ({ id, attributes: { idle, connected, user, remote } }) => ({
                    id,
                    user: `${user}@${remote}`,
                    connected: this.$help.dateFormat({ value: connected }),
                    idle,
                })
            )
        },
        filterSessionParam() {
            return this.getFilterParamByServiceId(this.$route.params.id)
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
            // when active tab is Parameters & Relationships
            if (val === 0) await this.fetchModuleParameters(this.routerModule)
        },
        // re-fetch when the route changes
        $route: async function() {
            await this.initialFetch()
            if (this.currentActiveTab === 0) await this.fetchModuleParameters(this.routerModule)
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
            fetchServiceDiagnostics: 'service/fetchServiceDiagnostics',
            fetchServiceConnections: 'service/fetchServiceConnections',
            genServiceConnectionsDataSets: 'service/genDataSets',
            updateServiceRelationship: 'service/updateServiceRelationship',
            updateServiceParameters: 'service/updateServiceParameters',
            fetchSessionsWithFilter: 'session/fetchSessionsWithFilter',
            fetchAllFilters: 'filter/fetchAllFilters',
        }),
        ...mapMutations({
            SET_FORM_TYPE: 'SET_FORM_TYPE',
            SET_REFRESH_RESOURCE: 'SET_REFRESH_RESOURCE',
        }),

        async initialFetch() {
            await this.fetchService()
            await this.genServiceConnectionsDataSets()
            await this.fetchConnSessDiag()
            await Promise.all([
                this.processRoutingTargetsTable(),
                this.processRelationshipTable('filters'),
                this.processRelationshipTable('listeners'),
            ])
        },
        // reuse functions for fetch loop or after finish editing
        async fetchService() {
            await this.fetchServiceById(this.$route.params.id)
        },

        /**
         * This function fetch current connection, session and service router_diagnostics
         */
        async fetchConnSessDiag() {
            const serviceId = this.$route.params.id
            // fetching connections chart info should be at the same time with fetchSessionsWithFilter
            await Promise.all([
                this.fetchServiceConnections(serviceId),
                this.fetchSessionsWithFilter(this.filterSessionParam),
                this.fetchServiceDiagnostics(serviceId),
            ])
        },
        async genRelationshipRows(type) {
            const { relationships: { [`${type}`]: { data = [] } = {} } = {} } = this.current_service
            let arr = []
            for (const obj of data) {
                const { attributes: { state = null } = {} } = await this.getRelationshipData(
                    type,
                    obj.id
                )
                let row = { id: obj.id, type, state }
                if (type === 'filters') delete row.state
                arr.push(row)
            }
            return arr
        },
        async processRoutingTargetsTable() {
            const { relationships = {} } = this.current_service
            let rows = []
            for (const key of Object.keys(relationships)) {
                if (this.ROUTING_TARGET_RELATIONSHIP_TYPES.includes(key)) {
                    const data = await this.genRelationshipRows(key)
                    rows = [...rows, ...data]
                }
            }
            this.routingTargetsTableRows = rows
        },
        /**
         * This function get relationship data based on relationship type i.e. filters, listeners.
         * It loops through id array to send sequential requests to get relationship state
         * @param {String} type- relationship type of resource. either filters or listeners
         */
        async processRelationshipTable(type) {
            this[`${type}TableRows`] = await this.genRelationshipRows(type)
        },

        /**
         * This function fetch all resource state if id is not provided
         * otherwise it fetch a resource state.
         * Even filter doesn't have state, the request still success
         * @param {String} type type of resource: listeners, filters
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
        async dispatchRelationshipUpdate({ type, data, isFilterDrag, isUpdatingRouteTarget }) {
            await this.updateServiceRelationship({
                id: this.current_service.id,
                type: type,
                data,
                callback: this.fetchService,
            })
            switch (type) {
                case 'filters':
                    if (!isFilterDrag) await this.processRelationshipTable(type)
                    break
            }
            if (isUpdatingRouteTarget) this.processRoutingTargetsTable()
        },
    },
}
</script>
