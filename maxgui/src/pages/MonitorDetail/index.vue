<template>
    <page-wrapper>
        <v-sheet v-if="!$help.lodash.isEmpty(current_monitor)" class="px-6">
            <page-header :currentMonitor="current_monitor" :onEditSucceeded="fetchMonitor" />
            <overview-header :currentMonitor="current_monitor" />
            <v-row>
                <!-- PARAMETERS TABLE -->
                <v-col cols="6">
                    <details-parameters-collapse
                        :searchKeyword="search_keyword"
                        :resourceId="current_monitor.id"
                        :parameters="current_monitor.attributes.parameters"
                        :moduleParameters="processedModuleParameters"
                        :updateResourceParameters="updateMonitorParameters"
                        :onEditSucceeded="fetchMonitor"
                        :loading="
                            loadingModuleParams
                                ? true
                                : overlay_type === OVERLAY_TRANSPARENT_LOADING
                        "
                    />
                </v-col>
                <v-col cols="6">
                    <relationship-table
                        relationshipType="servers"
                        :tableRows="serverStateTableRow"
                        :loading="overlay_type === OVERLAY_TRANSPARENT_LOADING"
                        :getRelationshipData="getRelationshipData"
                        @on-relationship-update="dispatchRelationshipUpdate"
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
import { mapActions, mapState } from 'vuex'
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
        ...mapState({
            overlay_type: 'overlay_type',
            search_keyword: 'search_keyword',
            module_parameters: 'module_parameters',
            current_monitor: state => state.monitor.current_monitor,
        }),
    },

    async created() {
        await this.fetchMonitor()
        await this.serverTableRowProcessing()
        const { attributes: { module: moduleName = null } = {} } = this.current_monitor
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
            if (this.module_parameters.length) {
                this.processedModuleParameters = this.module_parameters
                await this.$help.delay(150).then(() => (this.loadingModuleParams = false))
            }
        },

        async fetchMonitor() {
            await this.fetchMonitorById(this.$route.params.id)
        },

        async serverTableRowProcessing() {
            const {
                relationships: { servers: { data: serversData = [] } = {} } = {},
            } = this.current_monitor

            if (serversData.length) {
                let serversIdArr = serversData.map(item => `${item.id}`)
                let arr = []
                for (let i = 0; i < serversIdArr.length; ++i) {
                    let data = await this.getRelationshipData('servers', serversIdArr[i])
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
         * This function fetch all resource state if id is not provided
         * otherwise it fetch a resource state.
         * @param {String} type type of resource: servers
         * @param {String} id name of the resource (optional)
         * @return {Array} Resource state data
         */
        async getRelationshipData(type, id) {
            let data = await this.getResourceState({
                resourceId: id,
                resourceType: type,
                caller: 'monitor-detail-page-getRelationshipData',
            })
            return data
        },

        // actions to vuex
        async dispatchRelationshipUpdate({ type, data }) {
            await this.updateMonitorRelationship({
                id: this.current_monitor.id,
                [type]: data,
                callback: this.fetchMonitor,
            })
            await this.serverTableRowProcessing()
        },
    },
}
</script>
