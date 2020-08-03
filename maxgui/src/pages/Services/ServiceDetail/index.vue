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
                                <servers-filters-tables
                                    :serverStateTableRow="serverStateTableRow"
                                    :dispatchRelationshipUpdate="dispatchRelationshipUpdate"
                                    :loading="overlay === OVERLAY_TRANSPARENT_LOADING"
                                    :getServerState="getServerState"
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
import ServersFiltersTables from './ServersFiltersTables'
import SessionsTable from './SessionsTable'
import ParametersTable from './ParametersTable'
import DiagnosticsTable from './DiagnosticsTable'

export default {
    name: 'service-detail',
    components: {
        PageHeader,
        OverviewHeader,
        ServersFiltersTables,
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
        await this.serverTableRowProcessing()
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

        async serverTableRowProcessing() {
            const {
                relationships: { servers: { data: serversData = [] } = {} } = {},
            } = this.currentService

            if (serversData.length) {
                let serversIdArr = serversData.map(item => `${item.id}`)
                let arr = []
                for (let i = 0; i < serversIdArr.length; ++i) {
                    let data = await this.getServerState(serversIdArr[i])
                    const {
                        id,
                        type,
                        attributes: { state },
                    } = data
                    arr.push({ id: id, state: state, type: type })
                }
                this.serverStateTableRow = arr
            } else {
                this.serverStateTableRow = []
            }
        },

        /**
         * This function fetch all servers state, if serverId is not provided,
         * otherwise it fetch server state of a server based on serverId
         * @param {String} serverId name of the server
         * @return {Array} Server state data
         */
        async getServerState(serverId) {
            const data = this.getResourceState({
                resourceId: serverId,
                resourceType: 'servers',
                caller: 'service-detail-page-getServerState',
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
                    await this.serverTableRowProcessing()
                    break
            }
        },
    },
}
</script>
