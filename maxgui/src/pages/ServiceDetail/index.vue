<template>
    <page-wrapper>
        <v-sheet v-if="!$help.lodash.isEmpty(current_service)" class="pl-6">
            <page-header :currentService="current_service" :onEditSucceeded="fetchService" />
            <!--
                @update-chart is emitted every 10s
            -->
            <overview-header
                :currentService="current_service"
                :serviceConnectionsDatasets="service_connections_datasets"
                :serviceConnectionInfo="serviceConnectionInfo"
                @update-chart="fetchServiceAndSession"
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
                                                SET_FORM_TYPE(RESOURCE_FORM_TYPES.LISTENER)
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
                                    isTree
                                />
                            </v-col>
                            <v-col class="py-0 my-0" cols="6">
                                <sessions-table
                                    :search="search_keyword"
                                    :collapsible="true"
                                    :delayLoading="true"
                                    :rows="sessionsTableRows"
                                    :headers="sessionsTableHeader"
                                    :sortDesc="true"
                                    sortBy="connected"
                                    @confirm-kill="
                                        killSession({
                                            id: $event.id,
                                            callback: fetchSessionsFilterByService(serviceId),
                                        })
                                    "
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
 * Change Date: 2026-04-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
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
            currentActiveTab: null,
            tabs: [
                { name: `${this.$tc('parameters', 2)} & ${this.$tc('relationships', 2)}` },
                { name: `${this.$tc('sessions', 2)} & ${this.$tc('diagnostics', 2)}` },
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
            search_keyword: 'search_keyword',
            should_refresh_resource: 'should_refresh_resource',
            current_service: state => state.service.current_service,
            service_connections_datasets: state => state.service.service_connections_datasets,
            sessions_by_service: state => state.session.sessions_by_service,
            RESOURCE_FORM_TYPES: state => state.app_config.RESOURCE_FORM_TYPES,
        }),
        serviceId() {
            return this.$route.params.id
        },
        serviceConnectionInfo() {
            const { total_connections, connections } = this.$typy(
                this.current_service,
                'attributes.statistics'
            ).safeObjectOrEmpty
            return { total_connections, connections }
        },
        routerDiagnostics() {
            return this.$typy(this.current_service, 'attributes.router_diagnostics')
                .safeObjectOrEmpty
        },
        routerModule() {
            return this.current_service.attributes.router
        },
        sessionsTableRows() {
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
        async should_refresh_resource(val) {
            if (val) {
                this.SET_REFRESH_RESOURCE(false)
                await this.initialFetch()
            }
        },
        async currentActiveTab(val) {
            // when active tab is Parameters & Relationships
            if (val === 0) await this.fetchModuleParameters(this.routerModule)
        },
        // re-fetch when the route changes
        async $route() {
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
            genServiceConnectionsDataSets: 'service/genDataSets',
            updateServiceRelationship: 'service/updateServiceRelationship',
            updateServiceParameters: 'service/updateServiceParameters',
            fetchSessionsFilterByService: 'session/fetchSessionsFilterByService',
            fetchAllFilters: 'filter/fetchAllFilters',
            killSession: 'session/killSession',
        }),
        ...mapMutations({
            SET_FORM_TYPE: 'SET_FORM_TYPE',
            SET_REFRESH_RESOURCE: 'SET_REFRESH_RESOURCE',
        }),

        async initialFetch() {
            await this.fetchService()
            await this.genServiceConnectionsDataSets()
            await this.fetchServiceAndSession()
            await Promise.all([
                this.processingRelationshipTable('servers'),
                this.processingRelationshipTable('filters'),
                this.processingRelationshipTable('listeners'),
            ])
        },
        // reuse functions for fetch loop or after finish editing
        async fetchService() {
            await this.fetchServiceById(this.serviceId)
        },

        /**
         * This function fetch current connection, session and service router_diagnostics
         */
        async fetchServiceAndSession() {
            // fetching connections chart info should be at the same time with fetchSessionsFilterByService
            await Promise.all([
                this.fetchService(),
                this.fetchSessionsFilterByService(this.serviceId),
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

            for (const id of ids) {
                let data = await this.getRelationshipData(relationshipType, id)
                const { id: relationshipId, type, attributes: { state = null } = {} } = data
                let row = { id: relationshipId, type: type }
                if (relationshipType !== 'filters') row.state = state
                arr.push(row)
            }

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
