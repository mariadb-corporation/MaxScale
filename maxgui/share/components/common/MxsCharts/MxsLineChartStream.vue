<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-03-20
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import { Line } from 'vue-chartjs'
import base from '@share/components/common/MxsCharts/base.js'
import 'chartjs-plugin-streaming'
import { streamTooltip } from '@share/components/common/MxsCharts/customTooltips'
export default {
    extends: Line,
    mixins: [base],
    props: {
        refreshRate: { type: Number, required: true },
    },
    data() {
        return {
            uniqueTooltipId: this.$helpers.lodash.uniqueId('tooltip_'),
        }
    },
    computed: {
        baseOpts() {
            const scope = this
            return {
                showLines: true,
                layout: {
                    padding: {
                        left: 2,
                        bottom: 10,
                        right: 0,
                        top: 15,
                    },
                },
                legend: { display: false },
                responsive: true,
                maintainAspectRatio: false,
                elements: { point: { radius: 0 } },
                hover: { mode: 'index', intersect: false },
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
                    xAxes: [{ type: 'realtime', ticks: { display: false } }],
                    yAxes: [
                        {
                            gridLines: { zeroLineColor: 'transparent' },
                            ticks: { beginAtZero: true, maxTicksLimit: 3 },
                        },
                    ],
                },
                plugins: {
                    streaming: {
                        duration: this.refreshRate * 2000,
                        delay: (this.refreshRate + 2) * 1000,
                    },
                },
            }
        },
    },
    watch: {
        refreshRate(v, oV) {
            if (!this.$helpers.lodash.isEqual(v, oV)) {
                this.$data._chart.destroy()
                this.renderChart(this.chartData, this.options)
            }
        },
    },
    beforeDestroy() {
        let tooltipEl = document.getElementById(this.uniqueTooltipId)
        if (tooltipEl) tooltipEl.remove()
    },
}
</script>
