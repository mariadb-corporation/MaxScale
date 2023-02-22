<template>
    <line-chart
        ref="wrapper"
        v-bind="{ ...$attrs }"
        :style="{ width: '100%' }"
        :chartOptions="chartOptions"
        v-on="$listeners"
    />
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-02-21
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Chart from 'chart.js/auto'
import { Line } from 'vue-chartjs/legacy'
import ChartStreaming from 'chartjs-plugin-streaming'
import base from '@share/components/common/MxsCharts/base.js'
import { streamTooltip } from '@share/components/common/MxsCharts/customTooltips'

Chart.register(ChartStreaming)

export default {
    components: { 'line-chart': Line },
    mixins: [base],
    inheritAttrs: false,
    props: {
        refreshRate: { type: Number, default: -1 },
    },
    data() {
        return {
            uniqueTooltipId: this.$helpers.lodash.uniqueId('tooltip_'),
        }
    },
    computed: {
        chartOptions() {
            const scope = this
            const options = {
                showLine: true,
                elements: { point: { radius: 0 } },
                interaction: { mode: 'index', intersect: false },
                scales: {
                    x: {
                        type: 'realtime',
                        ticks: { display: false },
                    },
                    y: {
                        beginAtZero: true,
                        grid: { zeroLineColor: 'transparent' },
                        ticks: { maxTicksLimit: 3 },
                    },
                },
                plugins: {
                    streaming: {
                        duration: this.refreshRate * 2000,
                        delay: (this.refreshRate + 2) * 1000,
                    },
                    tooltip: {
                        mode: 'index',
                        intersect: false,
                        enabled: false,
                        external: context =>
                            streamTooltip({ context, tooltipId: scope.uniqueTooltipId }),
                    },
                },
            }
            return this.$helpers.lodash.merge(options, this.baseOpts)
        },
    },
    watch: {
        refreshRate(v, oV) {
            if (!this.$helpers.lodash.isEqual(v, oV)) this.chartInstance.update('quiet')
        },
    },
    beforeDestroy() {
        let tooltipEl = document.getElementById(this.uniqueTooltipId)
        if (tooltipEl) tooltipEl.remove()
    },
}
</script>
