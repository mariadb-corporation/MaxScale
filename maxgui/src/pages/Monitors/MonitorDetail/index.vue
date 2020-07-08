<template>
    <page-wrapper>
        <v-sheet v-if="!$help.lodash.isEmpty(currentMonitor)" class="px-6">
            <page-header :currentMonitor="currentMonitor" :onEditSucceeded="fetchMonitor" />
            <overview-header :currentMonitor="currentMonitor" />
            <v-row>
                <!-- PARAMETERS TABLE -->
                <v-col cols="6">
                    <details-parameters-collapse
                        :searchKeyWord="searchKeyWord"
                        :resourceId="currentMonitor.id"
                        :parameters="currentMonitor.attributes.parameters"
                        :moduleParameters="moduleParameters"
                        :requiredParams="['user', 'password']"
                        :updateResourceParameters="updateMonitorParameters"
                        :onEditSucceeded="fetchMonitor"
                        :loading="
                            loadingModuleParams ? true : overlay === OVERLAY_TRANSPARENT_LOADING
                        "
                    />
                </v-col>
                <servers-table
                    :searchKeyWord="searchKeyWord"
                    :currentMonitor="currentMonitor"
                    :getServers="getServers"
                    :loading="overlay === OVERLAY_TRANSPARENT_LOADING"
                    :dispatchRelationshipUpdate="dispatchRelationshipUpdate"
                    :serverStateTableRow="serverStateTableRow"
                />
            </v-row>
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
import PageHeader from './PageHeader'
import OverviewHeader from './OverviewHeader'
import ServersTable from './ServersTable'

export default {
    components: {
        PageHeader,
        OverviewHeader,

        ServersTable,
    },
    data() {
        return {
            OVERLAY_TRANSPARENT_LOADING: OVERLAY_TRANSPARENT_LOADING,
            serverStateTableRow: [],
            moduleParameters: [],
            loadingModuleParams: true,
        }
    },
    computed: {
        ...mapGetters({
            overlay: 'overlay',
            searchKeyWord: 'searchKeyWord',
            currentMonitor: 'monitor/currentMonitor',
        }),
    },

    async created() {
        let self = this
        await self.fetchAll()
        let res = await self.axios.get(
            `/maxscale/modules/${self.currentMonitor.attributes.module}?fields[module]=parameters`
        )
        const { attributes: { parameters = [] } = {} } = res.data.data
        self.moduleParameters = parameters

        self.loadingModuleParams = true
        await self.$help.delay(150).then(() => (self.loadingModuleParams = false))
    },

    methods: {
        ...mapActions('monitor', [
            'fetchMonitorById',
            'updateMonitorParameters',
            'updateMonitorRelationship',
        ]),

        async fetchAll() {
            await this.fetchMonitor()
            await this.fetchServerStateLoop()
        },

        async fetchMonitor() {
            await this.fetchMonitorById(this.$route.params.id)
        },

        async fetchServerStateLoop() {
            if (!this.$help.lodash.isEmpty(this.currentMonitor.relationships.servers)) {
                let servers = this.currentMonitor.relationships.servers.data
                let serversIdArr = servers ? servers.map(item => `${item.id}`) : []

                let arr = []
                for (let i = 0; i < serversIdArr.length; ++i) {
                    let data = await this.getServers(serversIdArr[i])
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

        // fetch server state for all servers or one server
        async getServers(serverId) {
            let res
            if (serverId) {
                res = await this.axios.get(`/servers/${serverId}?fields[servers]=state`)
            } else {
                /* this fetch all servers, if res.data.data have relationship,
                 prevent user from adding server to the current monitor*/
                res = await this.axios.get(`/servers?fields[servers]=state,monitors`)
            }

            return res.data.data
        },

        // actions to vuex
        async dispatchRelationshipUpdate(data) {
            let self = this
            await self.updateMonitorRelationship({
                id: self.currentMonitor.id,
                servers: data,
                callback: self.fetchAll,
            })
        },
    },
}
</script>
