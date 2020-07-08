<template>
    <page-wrapper>
        <v-sheet v-if="!$help.lodash.isEmpty(currentService)" class="px-6">
            <page-header :currentService="currentService" :onEditSucceeded="fetchService" />

            <overview-header
                :currentService="currentService"
                :fetchSessions="fetchSessions"
                :fetchNewConnectionsInfo="fetchNewConnectionsInfo"
            />
            <v-tabs v-model="currentActiveTab" class="tab-navigation-wrapper">
                <v-tab v-for="tab in tabs" :key="tab.name">
                    {{ tab.name }}
                </v-tab>

                <v-tabs-items v-model="currentActiveTab">
                    <v-tab-item class="pt-5">
                        <ServerSessionTab
                            :searchKeyWord="searchKeyWord"
                            :currentService="currentService"
                            :serverStateTableRow="serverStateTableRow"
                            :dispatchRelationshipUpdate="dispatchRelationshipUpdate"
                            :loading="overlay === OVERLAY_TRANSPARENT_LOADING"
                            :sessionsByService="sessionsByService"
                            :getServerState="getServerState"
                        />
                    </v-tab-item>
                    <!-- Parameters & Diagnostics tab -->
                    <v-tab-item class="pt-5">
                        <parameter-diagnostics-tab
                            :currentService="currentService"
                            :searchKeyWord="searchKeyWord"
                            :updateServiceParameters="updateServiceParameters"
                            :onEditSucceeded="fetchService"
                            :loading="overlay === OVERLAY_TRANSPARENT_LOADING"
                    /></v-tab-item>
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
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { OVERLAY_TRANSPARENT_LOADING } from 'store/overlayTypes'
import { mapGetters, mapActions } from 'vuex'
import OverviewHeader from './OverviewHeader'
import PageHeader from './PageHeader'
import ServerSessionTab from './ServerSessionTab'
import ParameterDiagnosticsTab from './ParameterDiagnosticsTab'

export default {
    name: 'service-detail',
    components: {
        PageHeader,
        OverviewHeader,
        ServerSessionTab,
        ParameterDiagnosticsTab,
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
            searchKeyWord: 'searchKeyWord',
            currentService: 'service/currentService',
            connectionInfo: 'service/connectionInfo',
            totalConnectionsChartData: 'service/totalConnectionsChartData',
            sessionsByService: 'session/sessionsByService',
        }),
    },

    async created() {
        let self = this
        // Initial fetch, wait for service id
        await Promise.all([self.fetchAll(), self.fetchSessions()])
        await self.genDataSetSchema()
    },
    methods: {
        ...mapActions({
            fetchServiceById: 'service/fetchServiceById',
            genDataSetSchema: 'service/genDataSetSchema',
            updateServiceRelationship: 'service/updateServiceRelationship',
            updateServiceParameters: 'service/updateServiceParameters',
            fetchServiceConnections: 'service/fetchServiceConnections',
            fetchSessionsFilterByServiceId: 'session/fetchSessionsFilterByServiceId',
        }),
        // call this when edit server table
        async fetchAll() {
            await this.fetchService()
            await this.serverStateTableRowProcessing()
        },
        // reuse functions for fetch loop or after finish editing
        async fetchService() {
            await this.fetchServiceById(this.$route.params.id)
        },

        async serverStateTableRowProcessing() {
            if (!this.$help.lodash.isEmpty(this.currentService.relationships.servers)) {
                let servers = this.currentService.relationships.servers.data
                let serversIdArr = servers ? servers.map(item => `${item.id}`) : []

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
        async fetchNewConnectionsInfo() {
            await this.fetchServiceConnections(this.$route.params.id)
        },
        async fetchSessions() {
            await this.fetchSessionsFilterByServiceId(this.$route.params.id)
        },
        // fetch server state for all servers or one server
        async getServerState(serverId) {
            let res
            if (serverId) {
                res = await this.axios.get(`/servers/${serverId}?fields[servers]=state`)
            } else {
                res = await this.axios.get(`/servers?fields[servers]=state`)
            }

            return res.data.data
        },
        // actions to vuex
        async dispatchRelationshipUpdate(type, data) {
            let self = this

            switch (type) {
                case 'filters':
                    // self.showOverlay(OVERLAY_TRANSPARENT_LOADING)
                    await self.updateServiceRelationship({
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
                    await self.updateServiceRelationship({
                        id: self.currentService.id,
                        type: 'servers',
                        servers: data,
                        callback: self.fetchAll,
                    })
                    break
            }
        },
    },
}
</script>
