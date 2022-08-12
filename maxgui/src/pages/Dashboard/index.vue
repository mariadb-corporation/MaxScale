<template>
    <page-wrapper>
        <v-sheet>
            <page-header>
                <template v-slot:refresh-rate>
                    <refresh-rate v-model="refreshRate" @on-count-done="onCountDone" />
                </template>
            </page-header>
            <graphs ref="graphs" :refreshRate="refreshRate" />
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
 * Change Date: 2026-08-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions } from 'vuex'
import TabNav from './TabNav'
import PageHeader from './PageHeader'
import Graphs from './Graphs'
import refreshRate from '@share/mixins/refreshRate'

export default {
    name: 'dashboard',
    components: {
        TabNav,
        PageHeader,
        Graphs,
    },
    mixins: [refreshRate],
    async created() {
        await this.fetchMaxScaleOverviewInfo()
        await this.fetchAll()
        // Generate datasets
        await Promise.all([
            this.genSessionDataSets(),
            this.genServersConnectionsDataSets(),
            this.genThreadsDataSets(),
        ])
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
        async fetchAll() {
            await Promise.all([
                this.fetchThreadStats(),
                this.fetchAllServers(),
                this.fetchAllMonitors(),
                this.fetchAllSessions(),
                this.fetchAllServices(),
                this.fetchAllListeners(),
                this.fetchAllFilters(),
            ])
        },
        async onCountDone() {
            const timestamp = Date.now()
            await this.fetchAll()
            // Update charts
            await this.$refs.graphs.updateChart(timestamp)
        },
    },
}
</script>
