<template>
    <v-sheet class="d-flex mb-2">
        <div class="d-flex" style="width:40%">
            <outlined-overview-card wrapperClass="mt-5">
                <template v-slot:title>
                    {{ $t('overview') }}
                </template>
                <template v-slot:card-body>
                    <span class="caption text-uppercase font-weight-bold color text-deep-ocean">
                        ROUTER
                    </span>
                    <span class="router text-no-wrap body-2">
                        {{ currentService.attributes.router }}
                    </span>
                </template>
            </outlined-overview-card>
            <outlined-overview-card wrapperClass="mt-5">
                <template v-slot:card-body>
                    <span class="caption text-uppercase font-weight-bold color text-deep-ocean">
                        STARTED AT
                    </span>
                    <span class="started text-no-wrap body-2">
                        {{ $help.dateFormat({ value: currentService.attributes.started }) }}
                    </span>
                </template>
            </outlined-overview-card>
        </div>
        <div style="width:60%" class="pl-3">
            <outlined-overview-card :tile="false" wrapperClass="mt-5">
                <template v-slot:title>
                    {{ $tc('currentConnections', 2) }}
                    <span class="text-lowercase font-weight-medium">
                        ({{ serviceConnectionInfo.connections }}/{{
                            serviceConnectionInfo.total_connections
                        }})</span
                    >
                </template>
                <template v-slot:card-body>
                    <v-sheet width="100%">
                        <line-chart
                            v-if="serviceConnectionsDatasets.length"
                            ref="serviceConnectionsChart"
                            :styles="{ height: '70px' }"
                            :chartData="{ datasets: serviceConnectionsDatasets }"
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
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    name: 'overview-header',
    props: {
        currentService: { type: Object, required: true },
        serviceConnectionsDatasets: { type: Array, required: true },
        serviceConnectionInfo: { type: Object, required: true },
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

    methods: {
        asyncEmit(eventName) {
            return new Promise(resolve => {
                this.$emit(eventName)
                this.$nextTick(resolve)
            })
        },
        async updateChart() {
            const { serviceConnectionsChart } = this.$refs
            if (serviceConnectionsChart) {
                await this.asyncEmit('update-chart')
                const { connections } = this.serviceConnectionInfo
                serviceConnectionsChart.chartData.datasets.forEach(function(dataset) {
                    dataset.data.push({
                        x: Date.now(),
                        y: connections,
                    })
                })

                serviceConnectionsChart.$data._chart.update({
                    preservation: true,
                })
            }
        },
    },
}
</script>
