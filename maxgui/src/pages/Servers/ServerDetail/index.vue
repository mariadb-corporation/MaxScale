<template>
    <page-wrapper>
        <v-sheet v-if="!$help.lodash.isEmpty(currentServer)" class="px-6">
            <page-header :currentServer="currentServer" :onEditSucceeded="fetchServer" />
            <overview-header
                :currentServer="currentServer"
                :updateServerRelationship="updateServerRelationship"
                :dispatchRelationshipUpdate="dispatchRelationshipUpdate"
            />
            <v-tabs v-model="currentActiveTab" class="tab-navigation-wrapper">
                <v-tab v-for="tab in tabs" :key="tab.name">
                    {{ tab.name }}
                </v-tab>

                <v-tabs-items v-model="currentActiveTab">
                    <v-tab-item class="pt-5">
                        <statistics-session-tab
                            :searchKeyWord="searchKeyWord"
                            :currentServer="currentServer"
                            :serviceTableRow="serviceTableRow"
                            :updateServerRelationship="updateServerRelationship"
                            :dispatchRelationshipUpdate="dispatchRelationshipUpdate"
                            :loading="overlay === OVERLAY_TRANSPARENT_LOADING"
                            :getServiceState="getServiceState"
                        />
                    </v-tab-item>
                    <!-- Parameters & Diagnostics tab -->
                    <v-tab-item class="pt-5">
                        <parameter-diagnostics-tab
                            :currentServer="currentServer"
                            :updateServerParameters="updateServerParameters"
                            :onEditSucceeded="fetchServer"
                            :loading="overlay === OVERLAY_TRANSPARENT_LOADING"
                            :searchKeyWord="searchKeyWord"
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
import PageHeader from './PageHeader'
import OverviewHeader from './OverviewHeader'
import ParameterDiagnosticsTab from './ParameterDiagnosticsTab'
import StatisticsSessionTab from './StatisticsSessionTab'
export default {
    name: 'server-detail',
    components: {
        PageHeader,
        OverviewHeader,
        StatisticsSessionTab,
        ParameterDiagnosticsTab,
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
        }
    },
    computed: {
        ...mapGetters({
            overlay: 'overlay',
            searchKeyWord: 'searchKeyWord',
            currentServer: 'server/currentServer',
        }),
    },

    async created() {
        // Initial fetch
        await this.fetchAll()
    },
    methods: {
        ...mapActions({
            fetchServerById: 'server/fetchServerById',
            updateServerRelationship: 'server/updateServerRelationship',
            updateServerParameters: 'server/updateServerParameters',
        }),
        // call this when edit service table
        async fetchAll() {
            await this.fetchServer()
            await this.serviceTableRowProcessing()
        },
        // reuse functions for fetch loop or after finish editing
        async fetchServer() {
            await this.fetchServerById(this.$route.params.id)
        },

        async serviceTableRowProcessing() {
            if (!this.$help.lodash.isEmpty(this.currentServer.relationships.services)) {
                let services = this.currentServer.relationships.services.data
                let servicesIdArr = services ? services.map(item => `${item.id}`) : []

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

        // fetch service state for all services or one service
        async getServiceState(serviceId) {
            let res
            if (serviceId) {
                res = await this.axios.get(`/services/${serviceId}?fields[services]=state`)
            } else {
                res = await this.axios.get(`/services?fields[services]=state`)
            }

            return res.data.data
        },
        // actions to vuex
        async dispatchRelationshipUpdate(type, data) {
            let self = this
            switch (type) {
                case 'monitors':
                    await self.updateServerRelationship({
                        id: self.currentServer.id,
                        type: 'monitors',
                        monitors: data,
                        callback: self.fetchServer,
                    })
                    break
                case 'services':
                    await self.updateServerRelationship({
                        id: self.currentServer.id,
                        type: 'services',
                        services: data,
                        callback: self.fetchAll,
                    })
                    break
            }
        },
    },
}
</script>
