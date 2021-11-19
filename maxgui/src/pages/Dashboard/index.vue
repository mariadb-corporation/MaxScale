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
 * Change Date: 2025-11-19
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
            this.fetchThreadStats(),
            this.fetchAllServers(),
            this.fetchAllMonitors(),
            this.fetchAllSessions(),
            this.fetchAllServices(),
        ])

        await Promise.all([
            this.genSessionDataSets(),
            this.genServersConnectionsDataSets(),
            this.genThreadsDataSets(),
        ])

        while (this.loop) {
            await Promise.all([
                this.fetchAllListeners(),
                this.fetchAllFilters(),
                this.$help.delay(10000),
            ])
        }
    },
    beforeDestroy() {
        this.loop = false
    },
    methods: {
        ...mapActions({
            fetchMaxScaleOverviewInfo: 'maxscale/fetchMaxScaleOverviewInfo',
            fetchThreadStats: 'maxscale/fetchThreadStats',
            genThreadsDataSets: 'maxscale/genDataSets',

            fetchAllServers: 'server/fetchAllServers',
            genServersConnectionsDataSets: 'server/genDataSets',

            fetchAllMonitors: 'monitor/fetchAllMonitors',

            fetchAllSessions: 'session/fetchAllSessions',
            genSessionDataSets: 'session/genDataSets',

            fetchAllServices: 'service/fetchAllServices',

            fetchAllListeners: 'listener/fetchAllListeners',

            fetchAllFilters: 'filter/fetchAllFilters',
        }),
    },
}
</script>
