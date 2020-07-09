<template>
    <page-wrapper>
        <v-sheet>
            <page-header />
            <graphs
                :fetchThreads="fetchThreads"
                :genThreadsDatasetsSchema="genThreadsDatasetsSchema"
                :fetchAllServers="fetchAllServers"
                :fetchAllSessions="fetchAllSessions"
                :fetchAllServices="fetchAllServices"
            />
            <tab-nav />
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
import { mapActions } from 'vuex'
import TabNav from './TabNav'
import PageHeader from './PageHeader'
import Graphs from './Graphs'

export default {
    name: 'dashboard',
    components: {
        TabNav,
        PageHeader,
        Graphs,
    },
    async created() {
        await Promise.all([
            this.fetchMaxScaleOverviewInfo(),
            this.fetchThreads(),
            this.fetchAllServers(),
            this.fetchAllMonitors(),
            this.fetchAllSessions(),
            this.fetchAllServices(),
        ])

        await Promise.all([
            this.genSessionChartDataSetSchema(),
            this.genServersConnectionsDataSetSchema(),
            this.genThreadsDatasetsSchema(),
        ])
    },

    methods: {
        ...mapActions({
            fetchMaxScaleOverviewInfo: 'maxscale/fetchMaxScaleOverviewInfo',
            fetchThreads: 'maxscale/fetchThreads',
            genThreadsDatasetsSchema: 'maxscale/genDataSetSchema',

            fetchAllServers: 'server/fetchAllServers',
            genServersConnectionsDataSetSchema: 'server/genDataSetSchema',

            fetchAllMonitors: 'monitor/fetchAllMonitors',

            fetchAllSessions: 'session/fetchAllSessions',
            genSessionChartDataSetSchema: 'session/genDataSetSchema',

            fetchAllServices: 'service/fetchAllServices',
        }),
    },
}
</script>
