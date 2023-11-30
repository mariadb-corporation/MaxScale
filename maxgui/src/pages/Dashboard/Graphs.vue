<template>
    <v-row class="mx-n2">
        <v-col cols="4" class="px-2">
            <outlined-overview-card :tile="false">
                <template v-slot:title>
                    {{ $mxs_tc('sessions', 2) }}
                </template>
                <template v-slot:card-body>
                    <mxs-line-chart-stream
                        v-if="sessions_datasets.length"
                        ref="sessionsChart"
                        class="relative pl-1"
                        :height="64"
                        :chartData="{ datasets: sessions_datasets }"
                        :refreshRate="refreshRate"
                    />
                </template>
            </outlined-overview-card>
        </v-col>
        <v-col cols="4" class="px-2">
            <outlined-overview-card :tile="false">
                <template v-slot:title>
                    {{ $mxs_tc('connections', 2) }}
                </template>
                <template v-if="all_servers.length" v-slot:card-body>
                    <mxs-line-chart-stream
                        v-if="server_connections_datasets.length"
                        ref="connsChart"
                        class="relative pl-1"
                        :height="64"
                        :chartData="{ datasets: server_connections_datasets }"
                        :refreshRate="refreshRate"
                    />
                </template>
            </outlined-overview-card>
        </v-col>
        <v-col cols="4" class="px-2">
            <outlined-overview-card :tile="false">
                <template v-slot:title>
                    {{ $mxs_t('load') }}
                </template>
                <template v-slot:card-body>
                    <mxs-line-chart-stream
                        v-if="threads_datasets.length"
                        ref="threadsChart"
                        class="relative pl-1"
                        :height="64"
                        :chartData="{ datasets: threads_datasets }"
                        :opts="{ scales: { y: { max: 100, min: 0 } } }"
                        :refreshRate="refreshRate"
                    />
                </template>
            </outlined-overview-card>
        </v-col>
    </v-row>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapGetters, mapState } from 'vuex'

export default {
    name: 'graphs',
    props: {
        refreshRate: { type: Number, required: true },
    },
    computed: {
        ...mapState({
            all_servers: state => state.server.all_servers,
            server_connections_datasets: state => state.server.server_connections_datasets,
            sessions_datasets: state => state.session.sessions_datasets,
            thread_stats: state => state.maxscale.thread_stats,
            threads_datasets: state => state.maxscale.threads_datasets,
        }),
        ...mapGetters({
            getTotalSessions: 'session/getTotalSessions',
        }),
    },
    methods: {
        updateSessionsGraph(chart, timestamp) {
            const self = this
            chart.data.datasets.forEach(function(dataset) {
                dataset.data.push({
                    x: timestamp,
                    y: self.getTotalSessions,
                })
            })
        },
        updateConnsGraph(chart, timestamp) {
            const scope = this
            const { genLineStreamDataset } = this.$helpers
            this.all_servers.forEach((server, i) => {
                const dataset = chart.data.datasets.find(d => d.resourceId === server.id)
                const value = scope.$typy(server, 'attributes.statistics.connections').safeNumber
                // update existing datasets
                if (dataset) dataset.data.push({ x: timestamp, y: value })
                else {
                    /*
                        Copy previous data of a dataset, this ensures new
                        datasets can be added while streaming datasets.
                        The value of each item should be 0 because
                        at previous timestamp, new servers aren't created yet.
                    */
                    let dataOfADataSet = scope.$typy(chart, 'data.datasets[0].data').safeArray
                    dataOfADataSet.forEach(item => (item.y = 0))
                    chart.data.datasets.push({
                        ...genLineStreamDataset({
                            label: `Server ID - ${server.id}`,
                            value,
                            colorIndex: i,
                            id: server.id,
                        }),
                        data: [...dataOfADataSet, { x: timestamp, y: value }],
                    })
                }
            })
        },
        updateThreadsGraph(chart, timestamp) {
            const { genLineStreamDataset } = this.$helpers
            const datasets = chart.data.datasets
            this.thread_stats.forEach((thread, i) => {
                const {
                    attributes: { stats: { load: { last_second = null } = {} } = {} } = {},
                } = thread

                if (chart.data.datasets[i]) {
                    datasets[i].data.push({
                        x: timestamp,
                        y: last_second,
                    })
                } else {
                    const newDataSet = genLineStreamDataset({
                        label: `THREAD ID - ${thread.id}`,
                        value: last_second,
                        colorIndex: i,
                        timestamp,
                    })
                    datasets.push(newDataSet)
                }
            })
        },
        /**
         * Method  to be called by parent component to update the chart
         */
        async updateChart() {
            const timestamp = Date.now()
            const sessionsChart = this.$typy(this.$refs, 'sessionsChart.chartInstance').safeObject
            const connsChart = this.$typy(this.$refs, 'connsChart.chartInstance').safeObject
            const threadsChart = this.$typy(this.$refs, 'threadsChart.chartInstance').safeObject

            if (sessionsChart && connsChart && threadsChart) {
                await Promise.all([
                    this.updateSessionsGraph(sessionsChart, timestamp),
                    this.updateConnsGraph(connsChart, timestamp),
                    this.updateThreadsGraph(threadsChart, timestamp),
                ])
            }
        },
    },
}
</script>
