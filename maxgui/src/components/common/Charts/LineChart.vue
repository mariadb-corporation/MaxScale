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
import { Line } from 'vue-chartjs'
export default {
    extends: Line,
    props: {
        chartData: { type: Object, required: true },
        options: { type: Object },
        hasVertCrossHair: { type: Boolean, default: false },
    },
    watch: {
        chartData() {
            this.$data._chart.destroy()
            this.renderLineChart()
        },
    },
    beforeDestroy() {
        if (this.$data._chart) this.$data._chart.destroy()
    },
    mounted() {
        if (this.hasVertCrossHair) {
            this.addPlugin({
                id: 'my-plugin',
                afterDatasetsDraw: function(chart) {
                    if (chart.tooltip._active && chart.tooltip._active.length) {
                        let activePoint = chart.tooltip._active[0],
                            ctx = chart.ctx,
                            y_axis = chart.scales['y-axis-0'],
                            x = activePoint.tooltipPosition().x,
                            topY = y_axis.top,
                            bottomY = y_axis.bottom
                        ctx.save()
                        ctx.beginPath()
                        ctx.moveTo(x, topY)
                        ctx.lineTo(x, bottomY)
                        ctx.lineWidth = 2
                        ctx.strokeStyle = '#0b718c'
                        ctx.stroke()
                        ctx.restore()
                    }
                },
            })
        }
        this.renderLineChart()
    },
    methods: {
        renderLineChart() {
            let chartOption = {
                tooltips: {
                    mode: 'index',
                    intersect: false,
                },
                elements: {
                    point: {
                        radius: 0,
                    },
                },
                scales: {
                    xAxes: [
                        {
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
