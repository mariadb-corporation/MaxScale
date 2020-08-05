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
                        :moduleParameters="processedModuleParameters"
                        :updateResourceParameters="updateMonitorParameters"
                        :onEditSucceeded="fetchMonitor"
                        :loading="
                            loadingModuleParams ? true : overlay === OVERLAY_TRANSPARENT_LOADING
                        "
                    />
                </v-col>
                <v-col cols="6">
                    <relationship-table
                        relationshipType="servers"
                        :tableRows="serverStateTableRow"
                        :dispatchRelationshipUpdate="dispatchRelationshipUpdate"
                        :loading="overlay === OVERLAY_TRANSPARENT_LOADING"
                        :getRelationshipData="getServersState"
                    />
                </v-col>
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
 * Change Date: 2024-07-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { OVERLAY_TRANSPARENT_LOADING } from 'store/overlayTypes'
import { mapGetters, mapActions } from 'vuex'
import PageHeader from './PageHeader'
import OverviewHeader from './OverviewHeader'

export default {
    components: {
        PageHeader,
        OverviewHeader,
    },
    data() {
        return {
            OVERLAY_TRANSPARENT_LOADING: OVERLAY_TRANSPARENT_LOADING,
            serverStateTableRow: [],
            processedModuleParameters: [],
            loadingModuleParams: true,
        }
    },
    computed: {
        ...mapGetters({
            overlay: 'overlay',
            searchKeyWord: 'searchKeyWord',
            moduleParameters: 'moduleParameters',
            currentMonitor: 'monitor/currentMonitor',
        }),
    },

    async created() {
        await this.fetchMonitor()
        await this.serverTableRowProcessing()
        const { attributes: { module: moduleName = null } = {} } = this.currentMonitor
        if (moduleName) await this.fetchModuleParameters(moduleName)
        this.loadingModuleParams = true
        await this.processModuleParameters()
    },

    methods: {
        ...mapActions({
            fetchModuleParameters: 'fetchModuleParameters',
            getResourceState: 'getResourceState',
            fetchMonitorById: 'monitor/fetchMonitorById',
            updateMonitorParameters: 'monitor/updateMonitorParameters',
            updateMonitorRelationship: 'monitor/updateMonitorRelationship',
        }),

        async processModuleParameters() {
            if (this.moduleParameters.length) {
                this.processedModuleParameters = this.moduleParameters
                const self = this
                await this.$help.delay(150).then(() => (self.loadingModuleParams = false))
            }
        },

        async fetchMonitor() {
            await this.fetchMonitorById(this.$route.params.id)
        },

        async serverTableRowProcessing() {
            const {
                relationships: { servers: { data: serversData = [] } = {} } = {},
            } = this.currentMonitor

            if (serversData.length) {
                let serversIdArr = serversData.map(item => `${item.id}`)
                let arr = []
                for (let i = 0; i < serversIdArr.length; ++i) {
                    let data = await this.getServersState(serversIdArr[i])
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
         * This function fetch all servers state, if serverId is provided,
         * otherwise it fetch server state of a server
         * @param {String} serverId name of the server
         * @return {Array} Server state data
         */
        async getServersState(id) {
            let data = await this.getResourceState({
                resourceId: id,
                resourceType: 'servers',
                caller: 'monitor-detail-page-getServersState',
            })
            return data
        },

        // actions to vuex
        async dispatchRelationshipUpdate(type, data) {
            const self = this
            await this.updateMonitorRelationship({
                id: self.currentMonitor.id,
                [type]: data,
                callback: self.fetchMonitor,
            })
            await this.serverTableRowProcessing()
        },
    },
}
</script>
