<template>
    <v-sheet class="d-flex mb-2">
        <div class="d-flex" style="width:40%">
            <outlined-overview-card cardWrapper="mt-5">
                <template v-slot:title>
                    {{ $t('overview') }}
                </template>
                <template v-slot:card-body>
                    <span class="caption text-uppercase font-weight-bold color text-deep-ocean">
                        ROUTER
                    </span>
                    <span class="text-no-wrap body-2">
                        {{ currentService.attributes.router }}
                    </span>
                </template>
            </outlined-overview-card>
            <outlined-overview-card cardWrapper="mt-5">
                <template v-slot:card-body>
                    <span class="caption text-uppercase font-weight-bold color text-deep-ocean">
                        STARTED AT
                    </span>
                    <span class="text-no-wrap body-2">
                        {{ $help.formatValue(currentService.attributes.started) }}
                    </span>
                </template>
            </outlined-overview-card>
        </div>
        <div style="width:60%" class="pl-3">
            <outlined-overview-card :tile="false" cardWrapper="mt-5">
                <template v-slot:title>
                    {{ $tc('currentConnections', 2) }}
                    <span class="text-lowercase font-weight-medium">
                        ({{ connectionInfo.connections }}/{{
                            connectionInfo.total_connections
                        }})</span
                    >
                </template>
                <template v-slot:card-body>
                    <v-sheet width="100%">
                        <line-chart
                            v-if="totalConnectionsChartData.datasets.length"
                            id="total-connections-chart"
                            ref="totalConnectionsChart"
                            :styles="{ height: '70px' }"
                            :chart-data="totalConnectionsChartData"
                            :options="options"
                        />
                    </v-sheet>
                </template>
            </outlined-overview-card>
        </div>
    </v-sheet>
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
    name: 'overview-header',

    props: {
        currentService: { type: Object, required: true },
        fetchSessions: { type: Function, required: true },
        fetchNewConnectionsInfo: { type: Function, required: true },
    },
    data() {
        return {
            options: {
                plugins: {
                    streaming: {
                        duration: 20000,
                        refresh: 10000, // onRefresh callback will be called every 10000 ms
                        /* delay of 10000 ms, so upcoming values are known before plotting a line
                      delay value can be larger but not smaller than refresh value to remain realtime streaming data */
                        delay: 10000,
                        onRefresh: this.updateChart,
                    },
                },
            },
        }
    },
    computed: {
        ...mapGetters({
            connectionInfo: 'service/connectionInfo',
            totalConnectionsChartData: 'service/totalConnectionsChartData',
        }),
    },

    methods: {
        async updateChart(chart) {
            let self = this
            // fetching connections chart info should be at the same time with fetchSessionsFilterByServiceId
            await Promise.all([self.fetchNewConnectionsInfo(), self.fetchSessions()])

            chart.data.datasets.forEach(function(dataset) {
                dataset.data.push({
                    x: Date.now(),
                    y: self.connectionInfo.connections,
                })
            })
            chart.update({
                preservation: true,
            })
        },
    },
}
</script>
