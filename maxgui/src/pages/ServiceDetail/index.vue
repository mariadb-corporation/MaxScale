<template>
    <page-wrapper>
        <v-sheet v-if="!$help.lodash.isEmpty(currentService)" class="px-6">
            <page-header :onEditSucceeded="fetchService" />
            <!--
                overview-header will fetch fetchServiceConnections and
                fetchSessionsFilterByServiceId parallelly.
                fetchSessionsFilterByServiceId will update sessions-table data
            -->
            <overview-header />

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
                                            relationshipType="servers"
                                            :tableRows="serverStateTableRow"
                                            :dispatchRelationshipUpdate="dispatchRelationshipUpdate"
                                            :loading="overlay === OVERLAY_TRANSPARENT_LOADING"
                                            :getRelationshipData="
                                                () => getRelationshipData('servers')
                                            "
                                        />
                                    </v-col>
                                    <v-col cols="12" class="pa-0 mt-4">
                                        <relationship-table
                                            relationshipType="filters"
                                            :tableRows="filtersTableRow"
                                            :dispatchRelationshipUpdate="dispatchRelationshipUpdate"
                                            :loading="overlay === OVERLAY_TRANSPARENT_LOADING"
                                            :getRelationshipData="
                                                () => getRelationshipData('filters')
                                            "
                                        />
                                    </v-col>

                                    <v-col cols="12" class="pa-0 mt-4">
                                        <relationship-table
                                            relationshipType="listeners"
                                            :loading="overlay === OVERLAY_TRANSPARENT_LOADING"
                                            :tableRows="listenerStateTableRow"
                                            readOnly
                                        />
                                    </v-col>
                                </v-row>
                            </v-col>
                            <v-col class="py-0 ma-0" cols="8">
                                <sessions-table
                                    :loading="overlay === OVERLAY_TRANSPARENT_LOADING"
                                />
                            </v-col>
                        </v-row>
                    </v-tab-item>
                    <!-- Parameters & Diagnostics tab -->
                    <v-tab-item class="pt-5">
                        <v-row>
                            <v-col class="py-0 my-0" cols="6">
                                <parameters-table
                                    :onEditSucceeded="fetchService"
                                    :loading="overlay === OVERLAY_TRANSPARENT_LOADING"
                                />
                            </v-col>
                            <diagnostics-table :loading="overlay === OVERLAY_TRANSPARENT_LOADING" />
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
 * Change Date: 2024-07-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { OVERLAY_TRANSPARENT_LOADING } from 'store/overlayTypes'
import { mapGetters, mapActions } from 'vuex'
import OverviewHeader from './OverviewHeader'
import PageHeader from './PageHeader'
import SessionsTable from './SessionsTable'
import ParametersTable from './ParametersTable'
import DiagnosticsTable from './DiagnosticsTable'

export default {
    name: 'service-detail',
    components: {
        PageHeader,
        OverviewHeader,
        SessionsTable,
        ParametersTable,
        DiagnosticsTable,
    },
    data() {
        return {
            OVERLAY_TRANSPARENT_LOADING: OVERLAY_TRANSPARENT_LOADING,
            currentActiveTab: null,
            tabs: [
                { name: `${this.$tc('servers', 2)} & ${this.$tc('sessions', 2)}` },
                { name: `${this.$tc('parameters', 2)} & ${this.$tc('diagnostics', 2)}` },
            ],
            serverStateTableRow: [],
            listenerStateTableRow: [],
            filtersTableRow: [],
        }
    },
    computed: {
        ...mapGetters({
            overlay: 'overlay',
            currentService: 'service/currentService',
            allFilters: 'filter/allFilters',
        }),
    },

    async created() {
        // Initial fetch, wait for service id
        await this.fetchService()
        await this.genDataSetSchema()
        await Promise.all([
            this.processingRelationshipTable('servers'),
            this.processingRelationshipTable('filters'),
            this.processingRelationshipTable('listeners'),
        ])
    },
    methods: {
        ...mapActions({
            getResourceState: 'getResourceState',
            fetchServiceById: 'service/fetchServiceById',
            genDataSetSchema: 'service/genDataSetSchema',
            updateServiceRelationship: 'service/updateServiceRelationship',
            fetchAllFilters: 'filter/fetchAllFilters',
        }),

        // reuse functions for fetch loop or after finish editing
        async fetchService() {
            await this.fetchServiceById(this.$route.params.id)
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
            } = this.currentService

            let ids = relationshipData.length ? relationshipData.map(item => `${item.id}`) : []
            let arr = []
            ids.forEach(async id => {
                let data = await this.getRelationshipData(relationshipType, id)
                const { id: relationshipId, type, attributes: { state = null } = {} } = data
                let row = { id: relationshipId, type: type }
                if (relationshipType !== 'filters') row.state = state
                arr.push(row)
            })

            switch (relationshipType) {
                case 'servers':
                    this.serverStateTableRow = arr
                    break
                case 'listeners':
                    this.listenerStateTableRow = arr
                    break
                case 'filters':
                    this.filtersTableRow = arr
            }
        },

        /**
         * This function fetch all resource state if id is not provided
         * otherwise it fetch a resource state.
         * Even filter doesn't have state, the request still success
         * @param {String} type type of resource. e.g. servers, listeners, filters
         * @param {String} id name of the resource
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
        async dispatchRelationshipUpdate(type, data, isFilterDrag) {
            const self = this
            await this.updateServiceRelationship({
                id: self.currentService.id,
                type: type,
                [type]: data,
                callback: self.fetchService,
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
