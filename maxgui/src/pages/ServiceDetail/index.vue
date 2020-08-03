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
                                <relationship-tables
                                    :serverStateTableRow="serverStateTableRow"
                                    :listenerStateTableRow="listenerStateTableRow"
                                    :dispatchRelationshipUpdate="dispatchRelationshipUpdate"
                                    :loading="overlay === OVERLAY_TRANSPARENT_LOADING"
                                    :getRelationshipState="getRelationshipState"
                                />
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
import RelationshipTables from './RelationshipTables'
import SessionsTable from './SessionsTable'
import ParametersTable from './ParametersTable'
import DiagnosticsTable from './DiagnosticsTable'

export default {
    name: 'service-detail',
    components: {
        PageHeader,
        OverviewHeader,
        RelationshipTables,
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
        }
    },
    computed: {
        ...mapGetters({
            overlay: 'overlay',
            currentService: 'service/currentService',
        }),
    },

    async created() {
        // Initial fetch, wait for service id
        await this.fetchService()
        await this.processingRelationshipTable('servers')
        await this.processingRelationshipTable('listeners')
        await this.genDataSetSchema()
    },
    methods: {
        ...mapActions({
            getResourceState: 'getResourceState',
            fetchServiceById: 'service/fetchServiceById',
            genDataSetSchema: 'service/genDataSetSchema',
            updateServiceRelationship: 'service/updateServiceRelationship',
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
                let data = await this.getRelationshipState(relationshipType, id)
                const {
                    id: relationshipId,
                    type,
                    attributes: { state },
                } = data
                arr.push({ id: relationshipId, state: state, type: type })
            })

            switch (relationshipType) {
                case 'servers':
                    this.serverStateTableRow = arr
                    break
                case 'listeners':
                    this.listenerStateTableRow = arr
            }
        },

        /**
         * This function fetch all resource state if id is not provided
         * otherwise it fetch a resource state
         * @param {String} type type of resource. e.g. servers, listeners
         * @param {String} id name of the resource
         * @return {Array} Resource state data
         */
        async getRelationshipState(type, id) {
            const data = this.getResourceState({
                resourceId: id,
                resourceType: type,
                caller: 'service-detail-page-getRelationshipState',
            })
            return data
        },

        // actions to vuex
        async dispatchRelationshipUpdate(type, data) {
            let self = this

            switch (type) {
                case 'filters':
                    // self.showOverlay(OVERLAY_TRANSPARENT_LOADING)
                    await this.updateServiceRelationship({
                        id: self.currentService.id,
                        type: 'filters',
                        filters: data,
                        callback: self.fetchService,
                    })
                    // // wait time out for loading animation
                    // await setTimeout(() => {
                    //     self.hideOverlay()
                    // }, 300)
                    break
                case 'servers':
                    await this.updateServiceRelationship({
                        id: self.currentService.id,
                        type: 'servers',
                        servers: data,
                        callback: self.fetchService,
                    })
                    await this.processingRelationshipTable('servers')
                    break
            }
        },
    },
}
</script>
