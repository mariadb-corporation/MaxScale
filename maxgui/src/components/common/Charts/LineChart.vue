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
    },
    data() {
        return {
            uniqueTooltipId: this.$help.lodash.uniqueId('tooltip_'),
        }
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
            const scope = this
            this.renderChart(this.chartData, {
                showLines: true,
                responsive: true,
                maintainAspectRatio: false,
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
                                drawBorder: true,
                                zeroLineColor: 'rgba(234, 234, 234, 1)',
                            },
                        },
                    ],
                    yAxes: [
                        {
                            gridLines: {
                                lineWidth: 0.6,
                                color: 'rgba(234, 234, 234,1)',
                                drawBorder: false,
                                zeroLineColor: 'transparent',
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
