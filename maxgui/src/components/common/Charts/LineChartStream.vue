<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-06-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { Line } from 'vue-chartjs'
import 'chartjs-plugin-streaming'
import { streamTooltip } from './customTooltips'
export default {
    extends: Line,
    props: {
        chartData: { type: Object, required: true },
        options: { type: Object },
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
            let chartOption = {
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
                        const chartScope = this
                        const position = chartScope._chart.canvas.getBoundingClientRect()
                        /** TODO: provide alignTooltipToLeft check to auto align tooltip position
                         *  For now, it is aligned to center
                         */
                        streamTooltip({
                            tooltipModel,
                            tooltipId: scope.uniqueTooltipId,
                            position,
                        })
                    },
                },
                scales: {
                    xAxes: [
                        {
                            gridLines: {
                                drawBorder: true,
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
                                drawBorder: false,
                                zeroLineColor: 'transparent',
                            },
                            ticks: {
                                beginAtZero: true,
                                maxTicksLimit: 3,
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
