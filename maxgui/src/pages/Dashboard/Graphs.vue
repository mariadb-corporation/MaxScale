<template>
    <v-row style="margin-right:-8px;margin-left:-8px">
        <v-col cols="4" style="padding:0px 8px">
            <outlined-overview-card :tile="false">
                <template v-slot:title>
                    {{ $tc('sessions', 2) }}
                </template>
                <template v-slot:card-body>
                    <v-sheet width="100%">
                        <line-chart
                            v-if="sessions_datasets.length"
                            ref="sessionsChart"
                            :styles="chartStyle"
                            :chart-data="{ datasets: sessions_datasets }"
                            :options="chartOptionsWithOutCallBack"
                        />
                    </v-sheet>
                </template>
            </outlined-overview-card>
        </v-col>
        <v-col cols="4" style="padding:0px 8px">
            <outlined-overview-card :tile="false">
                <template v-slot:title>
                    {{ $tc('connections', 2) }}
                </template>
                <template v-if="all_servers.length" v-slot:card-body>
                    <v-sheet width="100%">
                        <line-chart
                            v-if="server_connections_datasets.length"
                            ref="connectionsChart"
                            :styles="chartStyle"
                            :chart-data="{
                                datasets: server_connections_datasets,
                            }"
                            :options="chartOptionsWithOutCallBack"
                        />
                    </v-sheet>
                </template>
            </outlined-overview-card>
        </v-col>
        <v-col cols="4" style="padding:0px 8px">
            <outlined-overview-card :tile="false">
                <template v-slot:title>
                    {{ $t('load') }}
                </template>
                <template v-slot:card-body>
                    <v-sheet width="100%">
                        <line-chart
                            v-if="threads_datasets.length"
                            ref="threadsChart"
                            :styles="chartStyle"
                            :chart-data="{
                                datasets: threads_datasets,
                            }"
                            :options="mainChartOptions"
                            :yAxesTicks="{ max: 100, min: 0 }"
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
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapActions, mapState } from 'vuex'

export default {
    name: 'graphs',

    data() {
        return {
            chartStyle: { height: '70px', position: 'relative' },
            chartOptionsWithOutCallBack: {
                plugins: {
                    streaming: {
                        duration: 20000,
                        refresh: 10000,
                        delay: 10000,
                    },
                },
            },
            mainChartOptions: {
                plugins: {
                    streaming: {
                        duration: 20000,
                        refresh: 10000,
                        delay: 10000,

                        /*  delay of 10000 ms, so upcoming values are known before plotting a line
                            delay value can be larger but not smaller than refresh value to
                            remain realtime streaming data.
                            this onRefresh callback will be called every
                            10000 ms to update connections and sessions chart
                        */
                        onRefresh: this.updateChart,
                    },
                },
            },
        }
    },
    computed: {
        ...mapState('maxscale', {
            thread_stats: state => state.thread_stats,
            threads_datasets: state => state.threads_datasets,
        }),
        ...mapState('server', {
            server_connections_datasets: state => state.server_connections_datasets,
            all_servers: state => state.all_servers,
        }),
        ...mapState('session', {
            all_sessions: state => state.all_sessions,
            sessions_datasets: state => state.sessions_datasets,
        }),
    },

    methods: {
        ...mapActions({
            fetchThreadStats: 'maxscale/fetchThreadStats',
            fetchAllServers: 'server/fetchAllServers',
            fetchAllMonitors: 'monitor/fetchAllMonitors',
            fetchAllSessions: 'session/fetchAllSessions',
            fetchAllServices: 'service/fetchAllServices',
        }),

        updateServerConnectionsDatasets(connectionsChart, timestamp) {
            const {
                genLineDataSet,
                lodash: { xorWith, isEqual, cloneDeep },
            } = this.$help

            const connectionsChartDataSets = connectionsChart.chartData.datasets

            const currentServerIds = connectionsChartDataSets.map(dataset => dataset.resourceId)

            let currentServers = [] // current servers in datasets
            // get new data for current servers
            currentServerIds.forEach(id => {
                let server = this.all_servers.find(server => server.id === id)
                if (server) currentServers.push(server)
            })
            // update existing datasets
            currentServers.forEach((server, i) => {
                const {
                    attributes: {
                        statistics: { connections: serverConnections },
                    },
                } = server
                connectionsChartDataSets[i].data.push({
                    x: timestamp,
                    y: serverConnections,
                })
            })

            let newServers = []
            newServers = xorWith(this.all_servers, currentServers, isEqual)
            // push new datasets
            if (newServers.length) {
                newServers.forEach((server, i) => {
                    const {
                        attributes: {
                            statistics: { connections: serverConnections },
                        },
                    } = server

                    /*
                        Copy previous data of a dataset, this ensures new
                        datasets can be added while streaming datasets.
                        The value of each item should be 0 because
                        at previous timestamp, new servers aren't created yet.
                    */
                    let dataOfADataSet = cloneDeep(connectionsChartDataSets[0].data)
                    dataOfADataSet.forEach(item => (item.y = 0))

                    const newDataSet = genLineDataSet({
                        label: `Server ID - ${server.id}`,
                        value: serverConnections,
                        colorIndex: i,
                        timestamp,
                        id: server.id,
                        data: [...dataOfADataSet, { x: timestamp, y: serverConnections }],
                    })

                    connectionsChartDataSets.push(newDataSet)
                })
            }
        },

        updateSessionsDatasets(sessionsChart, timestamp) {
            const self = this
            sessionsChart.chartData.datasets.forEach(function(dataset) {
                dataset.data.push({
                    x: timestamp,
                    y: self.all_sessions.length,
                })
            })
        },

        updateThreadsDatasets(threadsChart, timestamp) {
            const { genLineDataSet } = this.$help
            const threadChartDataSets = threadsChart.chartData.datasets
            this.thread_stats.forEach((thread, i) => {
                const {
                    attributes: { stats: { load: { last_second = null } = {} } = {} } = {},
                } = thread

                if (threadsChart.chartData.datasets[i]) {
                    threadChartDataSets[i].data.push({
                        x: timestamp,
                        y: last_second,
                    })
                } else {
                    const newDataSet = genLineDataSet({
                        label: `THREAD ID - ${thread.id}`,
                        value: last_second,
                        colorIndex: i,
                        timestamp,
                    })
                    threadChartDataSets.push(newDataSet)
                }
            })
        },

        async updateChart() {
            const { sessionsChart, connectionsChart, threadsChart } = this.$refs
            if (sessionsChart && connectionsChart && threadsChart) {
                //  LOOP polling
                await Promise.all([
                    this.fetchAllServers(),
                    this.fetchAllMonitors(),
                    this.fetchAllSessions(),
                    this.fetchAllServices(),
                    this.fetchThreadStats(),
                ])
                const timestamp = Date.now()

                this.updateServerConnectionsDatasets(connectionsChart, timestamp)
                this.updateSessionsDatasets(sessionsChart, timestamp)
                this.updateThreadsDatasets(threadsChart, timestamp)

                await Promise.all([
                    sessionsChart.$data._chart.update({
                        preservation: true,
                    }),
                    threadsChart.$data._chart.update({
                        preservation: true,
                    }),
                    connectionsChart.$data._chart.update({
                        preservation: true,
                    }),
                ])
            }
        },
    },
}
</script>
