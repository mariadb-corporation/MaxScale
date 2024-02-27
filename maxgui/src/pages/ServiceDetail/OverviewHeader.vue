<template>
    <v-sheet class="d-flex mb-2">
        <div class="d-flex" style="width:40%">
            <outlined-overview-card wrapperClass="mt-5">
                <template v-slot:title>
                    {{ $mxs_t('overview') }}
                </template>
                <template v-slot:card-body>
                    <span
                        class="text-caption text-uppercase font-weight-bold mxs-color-helper text-deep-ocean"
                    >
                        ROUTER
                    </span>
                    <span class="router text-no-wrap text-body-2">
                        {{ currentService.attributes.router }}
                    </span>
                </template>
            </outlined-overview-card>
            <outlined-overview-card wrapperClass="mt-5">
                <template v-slot:card-body>
                    <span
                        class="text-caption text-uppercase font-weight-bold mxs-color-helper text-deep-ocean"
                    >
                        STARTED AT
                    </span>
                    <span class="started text-no-wrap text-body-2">
                        {{ $helpers.dateFormat({ value: currentService.attributes.started }) }}
                    </span>
                </template>
            </outlined-overview-card>
        </div>
        <div style="width:60%" class="pl-3">
            <outlined-overview-card :tile="false" wrapperClass="mt-5">
                <template v-slot:title>
                    {{ $mxs_tc('currentConnections', 2) }}
                    <span class="text-lowercase font-weight-medium">
                        ({{ serviceConnectionInfo.connections }}/{{
                            serviceConnectionInfo.total_connections
                        }})</span
                    >
                </template>
                <template v-slot:card-body>
                    <v-sheet width="100%">
                        <stream-line-chart
                            v-if="serviceConnectionsDatasets.length"
                            ref="connsChart"
                            :chartData="{ datasets: serviceConnectionsDatasets }"
                            :height="70"
                            :refreshRate="refreshRate"
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
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
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
        refreshRate: { type: Number, required: true },
    },
    methods: {
        updateChart() {
            const chart = this.$typy(this.$refs, 'connsChart.chartInstance').safeObject
            if (chart) {
                const { connections } = this.serviceConnectionInfo
                chart.data.datasets.forEach(dataset =>
                    dataset.data.push({ x: Date.now(), y: connections })
                )
            }
        },
    },
}
</script>
