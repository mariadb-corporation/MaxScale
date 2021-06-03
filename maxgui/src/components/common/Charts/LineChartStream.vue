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
import 'chartjs-plugin-streaming'
import customTooltip from './customTooltip'
export default {
    extends: Line,
    props: {
        chartData: {
            type: Object,
            required: true,
        },
        options: {
            type: Object,
        },
        yAxesTicks: {
            type: Object,
            default: () => {},
        },
    },
    data() {
        return {
            uniqueTooltipId: this.$help.lodash.uniqueId('tooltip_'),
        }
    },
    watch: {
        /* This chartData watcher doesn't make the chart reactivity, but it helps to
        destroy the chart when it's unmounted from the page. Eg: moving from dashboard page (have 3 charts)
        to service-detail page (1 chart), the chart will be destroyed and rerender to avoid
        several problems within vue-chartjs while using chartjs-plugin-streaming
        */
        chartData: function() {
            this.$data._chart.destroy()
            this.renderLineChart()
        },
    },
    beforeDestroy() {
        let tooltipEl = document.getElementById(this.uniqueTooltipId)
        tooltipEl && tooltipEl.remove()
        if (this.$data._chart) this.$data._chart.destroy()
    },
    mounted() {
        this.renderLineChart()
    },
    methods: {
        renderLineChart() {
            let scope = this
            this.renderChart(this.chartData, {
                showLines: true,
                layout: {
                    padding: {
                        left: 2,
                        bottom: 10,
                        right: 0,
                        top: 15,
                    },
                },
                legend: {
                    display: false,
                },
                responsive: true,
                maintainAspectRatio: false,
                elements: {
                    point: {
                        radius: 0,
                    },
                },
                hover: {
                    mode: 'index',
                    intersect: false,
                },
                tooltips: {
                    mode: 'index',
                    intersect: false,
                    enabled: false,
                    custom: function(tooltipModel) {
                        customTooltip({
                            tooltipModel,
                            tooltipId: scope.uniqueTooltipId,
                            scope: this,
                        })
                    },
                },
                scales: {
                    xAxes: [
                        {
                            gridLines: {
                                lineWidth: 0.6,
                                color: 'rgba(234, 234, 234, 1)',
                                drawTicks: false,
                                drawBorder: true,
                                zeroLineColor: 'rgba(234, 234, 234, 1)',
                            },
                            type: 'realtime',
                            ticks: {
                                display: false,
                            },
                        },
                    ],
                    yAxes: [
                        {
                            gridLines: {
                                lineWidth: 0.6,
                                color: 'rgba(234, 234, 234,1)',
                                drawTicks: false,
                                drawBorder: false,
                                zeroLineColor: 'transparent',
                            },
                            ticks: {
                                beginAtZero: true,
                                padding: 12,

                                maxTicksLimit: 3,
                                ...this.yAxesTicks,
                            },
                        },
                    ],
                },
                ...this.options,
            })
        },
    },
}
</script>
