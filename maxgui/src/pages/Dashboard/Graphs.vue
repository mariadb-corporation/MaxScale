<template>
    <v-row class="mx-n2">
        <v-col v-for="(graph, i) in graphs" :key="i" cols="4" class="px-2">
            <outlined-overview-card :tile="false" :height="graphCardHeight">
                <template v-slot:title>
                    <div class="d-flex align-center">
                        <span> {{ graph.title }} </span>

                        <v-spacer />
                        <template v-if="i === graphs.length - 1">
                            <v-btn
                                class="expand-toggle-btn mxs-color-helper text-anchor text-capitalize"
                                text
                                x-small
                                @click="isExpanded = !isExpanded"
                            >
                                {{ isExpanded ? $mxs_t('collapse') : $mxs_t('expand') }}
                            </v-btn>
                            <mxs-tooltip-btn
                                btnClass="setting-btn ml-1"
                                icon
                                x-small
                                color="primary"
                                @click="isDlgOpened = true"
                            >
                                <template v-slot:btn-content>
                                    <v-icon size="14">$vuetify.icons.mxs_settings</v-icon>
                                </template>
                                {{ $mxs_t('configuration') }}
                            </mxs-tooltip-btn>
                        </template>
                    </div>
                </template>
                <template v-slot:card-body>
                    <mxs-line-chart-stream
                        v-if="graph.datasets.length"
                        :ref="graph.name"
                        :style="chartStyle"
                        :chartData="{ datasets: graph.datasets }"
                        :refreshRate="refreshRate"
                        :opts="graph.opts"
                    />
                </template>
            </outlined-overview-card>
        </v-col>
        <graph-cnf-dlg v-model="isDlgOpened" />
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
import { mapGetters, mapState, mapMutations } from 'vuex'
import GraphCnfDlg from '@rootSrc/pages/Dashboard/GraphCnfDlg.vue'
export default {
    name: 'graphs',
    components: { GraphCnfDlg },
    props: {
        refreshRate: { type: Number, required: true },
    },
    data() {
        return {
            isDlgOpened: false,
        }
    },
    computed: {
        ...mapState({
            all_servers: state => state.server.all_servers,
            server_connections_datasets: state => state.server.server_connections_datasets,
            sessions_datasets: state => state.session.sessions_datasets,
            thread_stats: state => state.maxscale.thread_stats,
            threads_datasets: state => state.maxscale.threads_datasets,
            dsh_graphs_cnf: state => state.persisted.dsh_graphs_cnf,
            are_dsh_graphs_expanded: state => state.persisted.are_dsh_graphs_expanded,
        }),
        ...mapGetters({
            getTotalSessions: 'session/getTotalSessions',
        }),
        graphCardHeight() {
            if (this.isExpanded) return 75 * 4
            return 75
        },
        chartStyle() {
            return {
                height: `${this.graphCardHeight}px`,
            }
        },
        isExpanded: {
            get() {
                return this.are_dsh_graphs_expanded
            },
            set(v) {
                this.SET_ARE_DSH_GRAPHS_EXPANDED(v)
            },
        },
        baseOpts() {
            return {
                layout: {
                    padding: { top: 15, left: 4, bottom: -4 },
                },
                scales: {
                    y: {
                        ticks: { maxTicksLimit: this.isExpanded ? 6 : 3 },
                    },
                },
            }
        },
        graphs() {
            return [
                {
                    title: this.$mxs_tc('sessions', 2),
                    datasets: this.sessions_datasets,
                    name: 'sessions',
                    opts: this.$helpers.lodash.merge(
                        {
                            plugins: {
                                annotation: { annotations: this.getAnnotation('sessions') },
                            },
                        },
                        this.baseOpts
                    ),
                },
                {
                    title: this.$mxs_tc('connections', 2),
                    datasets: this.server_connections_datasets,
                    name: 'connections',
                    opts: this.$helpers.lodash.merge(
                        {
                            plugins: {
                                annotation: { annotations: this.getAnnotation('connections') },
                            },
                        },
                        this.baseOpts
                    ),
                },
                {
                    title: this.$mxs_t('load'),
                    datasets: this.threads_datasets,
                    name: 'load',
                    opts: this.$helpers.lodash.merge(
                        {
                            scales: { y: { max: 100, min: 0 } },
                            plugins: {
                                annotation: { annotations: this.getAnnotation('load') },
                            },
                        },
                        this.baseOpts
                    ),
                },
            ]
        },
    },
    methods: {
        ...mapMutations({ SET_ARE_DSH_GRAPHS_EXPANDED: 'persisted/SET_ARE_DSH_GRAPHS_EXPANDED' }),
        getAnnotation(graphName) {
            return this.$typy(this.dsh_graphs_cnf, `${graphName}.annotations`).safeObjectOrEmpty
        },
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
        updateLoadGraph(chart, timestamp) {
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
         * @public
         */
        async updateChart() {
            const timestamp = Date.now()
            const sessions = this.$typy(this.$refs, 'sessions[0].chartInstance').safeObject
            const connections = this.$typy(this.$refs, 'connections[0].chartInstance').safeObject
            const load = this.$typy(this.$refs, 'load[0].chartInstance').safeObject
            if (sessions && connections && load) {
                await Promise.all([
                    this.updateSessionsGraph(sessions, timestamp),
                    this.updateConnsGraph(connections, timestamp),
                    this.updateLoadGraph(load, timestamp),
                ])
            }
        },
    },
}
</script>

<style lang="scss" scoped>
.expand-toggle-btn {
    font-size: 0.875rem;
}
</style>
