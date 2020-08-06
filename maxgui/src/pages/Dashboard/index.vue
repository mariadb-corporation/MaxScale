<template>
    <page-wrapper>
        <v-sheet>
            <page-header />
            <graphs />
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
 * Change Date: 2024-07-16
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
    data() {
        return {
            loop: true,
        }
    },
    async created() {
        await Promise.all([
            this.fetchMaxScaleOverviewInfo(),
            // below fetches will be looped in graphs component
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

        while (this.loop) {
            await Promise.all([this.fetchAllListeners(), this.$help.delay(10000)])
        }
    },
    beforeDestroy() {
        this.loop = false
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

            fetchAllListeners: 'listener/fetchAllListeners',
        }),
    },
}
</script>
