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
import { Scatter } from 'vue-chartjs'
export default {
    extends: Scatter,
    props: {
        chartData: { type: Object, required: true },
        options: { type: Object },
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
        this.renderScatterChart()
    },
    methods: {
        renderScatterChart() {
            let chartOption = {
                plugins: {
                    streaming: false,
                },
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
