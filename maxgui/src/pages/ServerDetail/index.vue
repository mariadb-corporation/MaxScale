<template>
    <page-wrapper>
        <v-sheet v-if="!$help.lodash.isEmpty(current_server)" class="px-6">
            <page-header :onEditSucceeded="dispatchFetchServer" />
            <overview-header
                :getRelationshipData="getRelationshipData"
                @on-relationship-update="dispatchRelationshipUpdate"
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
                                    <statistics-table
                                        :loading="overlay_type === OVERLAY_TRANSPARENT_LOADING"
                                    />
                                    <v-col cols="12" class="pa-0 mt-4">
                                        <relationship-table
                                            relationshipType="services"
                                            :tableRows="serviceTableRow"
                                            :loading="overlay_type === OVERLAY_TRANSPARENT_LOADING"
                                            :getRelationshipData="getRelationshipData"
                                            @on-relationship-update="dispatchRelationshipUpdate"
                                        />
                                    </v-col>
                                </v-row>
                            </v-col>
                            <v-col class="py-0 ma-0" cols="8">
                                <sessions-table
                                    :loading="overlay_type === OVERLAY_TRANSPARENT_LOADING"
                                />
                            </v-col>
                        </v-row>
                    </v-tab-item>
                    <!-- Parameters & Diagnostics tab -->
                    <v-tab-item class="pt-5">
                        <v-row>
                            <v-col class="py-0 my-0" cols="6">
                                <parameters-table
                                    :onEditSucceeded="dispatchFetchServer"
                                    :loading="overlay_type === OVERLAY_TRANSPARENT_LOADING"
                                />
                            </v-col>
                            <v-col class="py-0 my-0" cols="6">
                                <diagnostics-table
                                    :loading="overlay_type === OVERLAY_TRANSPARENT_LOADING"
                                    :fetchMonitorDiagnostics="fetchMonitorDiagnostics"
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
 * Change Date: 2024-07-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { OVERLAY_TRANSPARENT_LOADING } from 'store/overlayTypes'
import { mapActions, mapMutations, mapState } from 'vuex'
import PageHeader from './PageHeader'
import OverviewHeader from './OverviewHeader'
import StatisticsTable from './StatisticsTable'
import SessionsTable from './SessionsTable'
import ParametersTable from './ParametersTable'
import DiagnosticsTable from './DiagnosticsTable'

export default {
    name: 'server-detail',
    components: {
        PageHeader,
        OverviewHeader,
        StatisticsTable,
        SessionsTable,
        ParametersTable,
        DiagnosticsTable,
    },

    data() {
        return {
            OVERLAY_TRANSPARENT_LOADING: OVERLAY_TRANSPARENT_LOADING,
            currentActiveTab: null,

            tabs: [
                { name: `${this.$tc('statistics', 2)} & ${this.$tc('sessions', 2)}` },
                { name: `${this.$tc('parameters', 2)} & ${this.$tc('diagnostics', 2)}` },
            ],
            serviceTableRow: [],
            //MONITOR data for parameter-diagnostics-tab
            monitorDiagnosticsTableRow: [],
        }
    },
    computed: {
        ...mapState({
            should_refresh_resource: 'should_refresh_resource',
            search_keyword: 'search_keyword',
            overlay_type: 'overlay_type',
            current_server: state => state.server.current_server,
        }),
    },
    watch: {
        should_refresh_resource: async function(val) {
            if (val) {
                this.SET_REFRESH_RESOURCE(false)
                await this.initialFetch()
            }
        },
    },
    async created() {
        await this.initialFetch()
    },
    methods: {
        ...mapMutations({
            SET_REFRESH_RESOURCE: 'SET_REFRESH_RESOURCE',
            SET_CURRENT_MONITOR: 'monitor/SET_CURRENT_MONITOR',
        }),
        ...mapActions({
            getResourceState: 'getResourceState',
            fetchServerById: 'server/fetchServerById',
            updateServerRelationship: 'server/updateServerRelationship',
            fetchMonitorDiagnosticsById: 'monitor/fetchMonitorDiagnosticsById',
        }),
        async initialFetch() {
            // Initial fetch
            await this.dispatchFetchServer()
            await this.serviceTableRowProcessing()
        },
        async fetchMonitorDiagnostics() {
            const { relationships: { monitors = {} } = {} } = this.current_server
            if (monitors.data) {
                const monitorId = monitors.data[0].id
                await this.fetchMonitorDiagnosticsById(monitorId)
            } else {
                this.SET_CURRENT_MONITOR({})
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
            if (servicesData.length) {
                let servicesIdArr = servicesData.map(item => `${item.id}`)
                let arr = []
                for (let i = 0; i < servicesIdArr.length; ++i) {
                    let data = await this.getRelationshipData('services', servicesIdArr[i])
                    const {
                        id,
                        type,
                        attributes: { state },
                    } = data
                    arr.push({ id: id, state: state, type: type })
                }
                this.serviceTableRow = arr
            } else {
                this.serviceTableRow = []
            }
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
                type: type,
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
