<template>
    <v-row class="mx-n2">
        <v-col cols="4" class="px-2">
            <outlined-overview-card :tile="false">
                <template v-slot:title>
                    {{ $mxs_tc('sessions', 2) }}
                </template>
                <template v-slot:card-body>
                    <v-sheet width="100%">
                        <mxs-line-chart-stream
                            v-if="sessions_datasets.length"
                            ref="sessionsChart"
                            class="relative"
                            :height="70"
                            :chart-data="{ datasets: sessions_datasets }"
                            :refreshRate="refreshRate"
                        />
                    </v-sheet>
                </template>
            </outlined-overview-card>
        </v-col>
        <v-col cols="4" class="px-2">
            <outlined-overview-card :tile="false">
                <template v-slot:title>
                    {{ $mxs_tc('connections', 2) }}
                </template>
                <template v-if="all_servers.length" v-slot:card-body>
                    <v-sheet width="100%">
                        <mxs-line-chart-stream
                            v-if="server_connections_datasets.length"
                            ref="connectionsChart"
                            class="relative"
                            :height="70"
                            :chart-data="{
                                datasets: server_connections_datasets,
                            }"
                            :refreshRate="refreshRate"
                        />
                    </v-sheet>
                </template>
            </outlined-overview-card>
        </v-col>
        <v-col cols="4" class="px-2">
            <outlined-overview-card :tile="false">
                <template v-slot:title>
                    {{ $mxs_t('load') }}
                </template>
                <template v-slot:card-body>
                    <v-sheet width="100%">
                        <mxs-line-chart-stream
                            v-if="threads_datasets.length"
                            ref="threadsChart"
                            class="relative"
                            :height="70"
                            :chart-data="{
                                datasets: threads_datasets,
                            }"
                            :opts="{ scales: { yAxes: [{ ticks: { max: 100, min: 0 } }] } }"
                            :refreshRate="refreshRate"
                        />
                    </v-sheet>
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
 * Change Date: 2028-01-30
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
        update(chart) {
            chart.$data._chart.update({ preservation: true })
        },
        updateSessionsGraph(chart, timestamp) {
            const self = this
            chart.chartData.datasets.forEach(function(dataset) {
                dataset.data.push({
                    x: timestamp,
                    y: self.getTotalSessions,
                })
            })
            this.update(chart)
        },
        updateConnsGraph(chart, timestamp) {
            const scope = this
            const { genLineStreamDataset } = this.$helpers
            this.all_servers.forEach((server, i) => {
                const dataset = chart.chartData.datasets.find(d => d.resourceId === server.id)
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
                    let dataOfADataSet = scope.$typy(chart, 'chartData.datasets[0].data').safeArray
                    dataOfADataSet.forEach(item => (item.y = 0))
                    chart.chartData.datasets.push({
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
            this.update(chart)
        },
        updateThreadsGraph(chart, timestamp) {
            const { genLineStreamDataset } = this.$helpers
            const datasets = chart.chartData.datasets
            this.thread_stats.forEach((thread, i) => {
                const {
                    attributes: { stats: { load: { last_second = null } = {} } = {} } = {},
                } = thread

                if (chart.chartData.datasets[i]) {
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
            this.update(chart)
        },
        /**
         * Method  to be called by parent component to update the chart
         */
        async updateChart() {
            const timestamp = Date.now()
            const { sessionsChart, connectionsChart, threadsChart } = this.$refs
            if (sessionsChart && connectionsChart && threadsChart) {
                await Promise.all([
                    this.updateConnsGraph(connectionsChart, timestamp),
                    this.updateSessionsGraph(sessionsChart, timestamp),
                    this.updateThreadsGraph(threadsChart, timestamp),
                ])
            }
        },
    },
}
</script>
