<template>
    <page-wrapper>
        <v-sheet v-if="!$help.lodash.isEmpty(currentServer)" class="px-6">
            <page-header :onEditSucceeded="fetchServer" />
            <overview-header
                :dispatchRelationshipUpdate="dispatchRelationshipUpdate"
                :getResourceState="getResourceState"
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
                                        :loading="overlay === OVERLAY_TRANSPARENT_LOADING"
                                    />
                                    <services-table
                                        :searchKeyWord="searchKeyWord"
                                        :serviceTableRow="serviceTableRow"
                                        :dispatchRelationshipUpdate="dispatchRelationshipUpdate"
                                        :loading="overlay === OVERLAY_TRANSPARENT_LOADING"
                                        :getServiceState="getServiceState"
                                    />
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
                                    :onEditSucceeded="fetchServer"
                                    :loading="overlay === OVERLAY_TRANSPARENT_LOADING"
                                />
                            </v-col>
                            <v-col class="py-0 my-0" cols="6">
                                <diagnostics-table
                                    :loading="overlay === OVERLAY_TRANSPARENT_LOADING"
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
import { mapGetters, mapActions, mapMutations } from 'vuex'
import PageHeader from './PageHeader'
import OverviewHeader from './OverviewHeader'
import StatisticsTable from './StatisticsTable'
import ServicesTable from './ServicesTable'
import SessionsTable from './SessionsTable'
import ParametersTable from './ParametersTable'
import DiagnosticsTable from './DiagnosticsTable'

export default {
    name: 'server-detail',
    components: {
        PageHeader,
        OverviewHeader,
        StatisticsTable,
        ServicesTable,
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
        ...mapGetters({
            overlay: 'overlay',
            searchKeyWord: 'searchKeyWord',
            currentServer: 'server/currentServer',
            currentMonitorDiagnostics: 'monitor/currentMonitorDiagnostics',
        }),
    },

    async created() {
        // Initial fetch
        await this.fetchServer()
        await this.serviceTableRowProcessing()
    },
    methods: {
        ...mapMutations({
            setCurrentMonitorDiagnostics: 'monitor/setCurrentMonitorDiagnostics',
        }),
        ...mapActions({
            getResourceState: 'getResourceState',
            fetchServerById: 'server/fetchServerById',
            updateServerRelationship: 'server/updateServerRelationship',
            fetchMonitorDiagnosticsById: 'monitor/fetchMonitorDiagnosticsById',
        }),

        async fetchMonitorDiagnostics() {
            const { relationships: { monitors = {} } = {} } = this.currentServer
            if (monitors.data) {
                const monitorId = monitors.data[0].id
                await this.fetchMonitorDiagnosticsById(monitorId)
            } else {
                this.setCurrentMonitorDiagnostics({})
            }
        },
        // reuse functions for fetch loop or after finish editing
        async fetchServer() {
            await this.fetchServerById(this.$route.params.id)
        },

        async serviceTableRowProcessing() {
            const {
                relationships: { services: { data: servicesData = [] } = {} } = {},
            } = this.currentServer
            if (servicesData.length) {
                let servicesIdArr = servicesData.map(item => `${item.id}`)
                let arr = []
                for (let i = 0; i < servicesIdArr.length; ++i) {
                    let data = await this.getServiceState(servicesIdArr[i])
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
         * This function fetch all services state, if serviceId is provided,
         * otherwise it fetch service state of a service
         * @param {String} serviceId name of the service
         * @return {Array} Service state data
         */
        async getServiceState(serviceId) {
            const data = this.getResourceState({
                resourceId: serviceId,
                resourceType: 'services',
                caller: 'server-detail-page-getServiceState',
            })
            return data
        },
        // actions to vuex
        async dispatchRelationshipUpdate(type, data) {
            let self = this
            switch (type) {
                case 'monitors':
                    await this.updateServerRelationship({
                        id: self.currentServer.id,
                        type: 'monitors',
                        monitors: data,
                        callback: self.fetchServer,
                    })
                    await this.fetchMonitorDiagnostics()
                    break
                case 'services':
                    await this.updateServerRelationship({
                        id: self.currentServer.id,
                        type: 'services',
                        services: data,
                        callback: self.fetchServer,
                    })
                    await this.serviceTableRowProcessing()
                    break
            }
        },
    },
}
</script>
