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
                            v-if="sessionsChartData.datasets.length"
                            id="sessions-Chart"
                            ref="sessionsChart"
                            :styles="{ height: '70px', position: 'relative' }"
                            :chart-data="sessionsChartData"
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
                <template v-if="allServers.length" v-slot:card-body>
                    <v-sheet width="100%">
                        <line-chart
                            v-if="serversConnectionsChartData.datasets.length"
                            id="servers-connection-Chart"
                            ref="connectionsChart"
                            :styles="{ height: '70px', position: 'relative' }"
                            :chart-data="serversConnectionsChartData"
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
                            v-if="threadsChartData.datasets.length"
                            id="threads-Chart"
                            ref="threadsChart"
                            :styles="{ height: '70px', position: 'relative' }"
                            :chart-data="threadsChartData"
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
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapGetters } from 'vuex'

export default {
    props: {
        fetchThreads: { type: Function, required: true },
        genThreadsDatasetsSchema: { type: Function, required: true },
        fetchAllServers: { type: Function, required: true },
        fetchAllSessions: { type: Function, required: true },
        fetchAllServices: { type: Function, required: true },
    },
    data() {
        return {
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
        ...mapGetters({
            maxScaleOverviewInfo: 'maxscale/maxScaleOverviewInfo',
            threads: 'maxscale/threads',
            threadsChartData: 'maxscale/threadsChartData',
            allSessions: 'session/allSessions',
            sessionsChartData: 'session/sessionsChartData',
            allServers: 'server/allServers',
            serversConnectionsChartData: 'server/serversConnectionsChartData',
        }),
    },

    methods: {
        //----------------------- Graphs update

        async updateChart() {
            let self = this
            const { sessionsChart, connectionsChart, threadsChart } = self.$refs
            if (sessionsChart && connectionsChart && threadsChart) {
                //  LOOP polling
                await Promise.all([
                    self.fetchAllServers(),
                    self.fetchAllSessions(),
                    self.fetchAllServices(),
                    self.fetchThreads(),
                ])
                const time = Date.now()
                //-------------------- update connections chart

                let gap = self.allServers.length - connectionsChart.chartData.datasets.length

                self.allServers.forEach((server, i) => {
                    if (gap > 0 && i > connectionsChart.chartData.datasets.length - 1) {
                        // push new datasets
                        let lineColors = self.$help.dynamicColors(i)
                        let indexOfOpacity = lineColors.lastIndexOf(')') - 1
                        let dataset = {
                            label: `Server ID - ${server.id}`,
                            id: `Server ID - ${server.id}`,
                            type: 'line',
                            // background of the line
                            backgroundColor: self.$help.strReplaceAt(
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
                        y: self.allSessions.length,
                    })
                })

                //-------------------- update threads chart
                await self.threads.forEach((thread, i) => {
                    if (self.$help.isUndefined(threadsChart.chartData.datasets[i])) {
                        self.genThreadsDatasetsSchema()
                    } else {
                        threadsChart.chartData.datasets[i].data.push({
                            x: Date.now(),
                            y: thread.attributes.stats.load.last_second,
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
