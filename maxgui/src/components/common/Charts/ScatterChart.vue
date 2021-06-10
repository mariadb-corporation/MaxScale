<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-05-25
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { Scatter } from 'vue-chartjs'
import vertCrossHair from './vertCrossHair'
export default {
    extends: Scatter,
    props: {
        chartData: { type: Object, required: true },
        options: { type: Object },
        hasVertCrossHair: { type: Boolean, default: false },
    },
    watch: {
        chartData() {
            this.$data._chart.destroy()
            this.renderScatterChart()
        },
    },
    beforeDestroy() {
        if (this.$data._chart) this.$data._chart.destroy()
    },
    mounted() {
        if (this.hasVertCrossHair) {
            this.addPlugin({
                id: 'vert-cross-hair',
                afterDatasetsDraw: vertCrossHair,
            })
        }
        this.renderScatterChart()
    },
    methods: {
        renderScatterChart() {
            let chartOption = {
                scales: {
                    xAxes: [
                        {
                            type: 'linear',
                            position: 'bottom',
                            gridLines: {
                                drawBorder: true,
                            },
                            ticks: {
                                beginAtZero: true,
                            },
                        },
                    ],
                    yAxes: [
                        {
                            gridLines: {
                                drawBorder: false,
                            },
                            ticks: {
                                beginAtZero: true,
                            },
                        },
                    ],
                },
            }
            this.renderChart(this.chartData, this.$help.lodash.deepMerge(chartOption, this.options))
        },
    },
}
</script>
