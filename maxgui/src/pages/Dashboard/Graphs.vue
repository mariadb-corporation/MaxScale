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
                            v-if="sessions_chart_data.datasets.length"
                            ref="sessionsChart"
                            :styles="chartStyle"
                            :chart-data="sessions_chart_data"
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
                            v-if="server_connections_chart_data.datasets.length"
                            ref="connectionsChart"
                            :styles="chartStyle"
                            :chart-data="server_connections_chart_data"
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
                            v-if="threads_chart_data.datasets.length"
                            ref="threadsChart"
                            :styles="chartStyle"
                            :chart-data="threads_chart_data"
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
 * Change Date: 2024-07-16
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
            threads_chart_data: state => state.threads_chart_data,
        }),
        ...mapState('server', {
            server_connections_chart_data: state => state.server_connections_chart_data,
            all_servers: state => state.all_servers,
        }),
        ...mapState('session', {
            all_sessions: state => state.all_sessions,
            sessions_chart_data: state => state.sessions_chart_data,
        }),
    },

    methods: {
        ...mapActions({
            fetchThreadStats: 'maxscale/fetchThreadStats',
            genThreadsDatasetsSchema: 'maxscale/genDataSetSchema',
            fetchAllServers: 'server/fetchAllServers',
            fetchAllMonitors: 'monitor/fetchAllMonitors',
            fetchAllSessions: 'session/fetchAllSessions',
            fetchAllServices: 'service/fetchAllServices',
        }),
        //----------------------- Graphs update

        async updateChart() {
            const self = this
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
                const time = Date.now()
                //-------------------- update connections chart

                let gap = this.all_servers.length - connectionsChart.chartData.datasets.length
                this.all_servers.forEach((server, i) => {
                    if (gap > 0 && i > connectionsChart.chartData.datasets.length - 1) {
                        // push new datasets
                        let lineColors = this.$help.dynamicColors(i)
                        let indexOfOpacity = lineColors.lastIndexOf(')') - 1
                        let dataset = {
                            label: `Server ID - ${server.id}`,
                            id: `Server ID - ${server.id}`,
                            type: 'line',
                            // background of the line
                            backgroundColor: this.$help.strReplaceAt(
                                lineColors,
                                indexOfOpacity,
                                '0.2'
                            ),
                            borderColor: lineColors, //theme.palette.primary.main, // line color
                            borderWidth: 1,
                            lineTension: 0,
                            data: [
                                {
                                    x: time,
                                    y: server.attributes.statistics.connections,
                                },
                            ],
                        }

                        connectionsChart.chartData.datasets.push(dataset)
                    } else {
                        connectionsChart.chartData.datasets[i].data.push({
                            x: time,
                            y: server.attributes.statistics.connections,
                        })
                    }
                })

                // ------------------------- update sessions chart

                sessionsChart.chartData.datasets.forEach(function(dataset) {
                    dataset.data.push({
                        x: time,
                        y: self.all_sessions.length,
                    })
                })

                //-------------------- update threads chart
                await this.thread_stats.forEach((thread, i) => {
                    if (this.$help.isUndefined(threadsChart.chartData.datasets[i])) {
                        this.genThreadsDatasetsSchema()
                    } else {
                        const {
                            attributes: { stats: { load: { last_second = null } = {} } = {} } = {},
                        } = thread
                        threadsChart.chartData.datasets[i].data.push({
                            x: time,
                            y: last_second,
                        })
                    }
                })

                sessionsChart.$data._chart.update({
                    preservation: true,
                })
                threadsChart.$data._chart.update({
                    preservation: true,
                })

                connectionsChart.$data._chart.update({
                    preservation: true,
                })
            }
        },
    },
}
</script>
