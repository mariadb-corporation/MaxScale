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
    watch: {
        chartData() {
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
            const scope = this
            let chartOption = {
                tooltips: {
                    mode: 'index',
                    intersect: false,
                    enabled: false,
                    custom: function(tooltipModel) {
                        /** TODO: Create another function for showing object tooltip
                         *  and rename customTooltip to strTooltip
                         */
                        customTooltip({
                            tooltipModel,
                            tooltipId: scope.uniqueTooltipId,
                            scope: this,
                        })
                    },
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
