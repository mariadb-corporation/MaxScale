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
 * Change Date: 2028-02-27
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
            streamingOpt: {
                delay: 2000,
                frameRate: 24,
            },
        }
    },
    computed: {
        isPaused() {
            return this.refreshRate === -1
        },

        chartOptions() {
            const scope = this
            return this.$helpers.lodash.merge(
                {
                    elements: { point: { radius: 0 } },
                    scales: {
                        x: { type: 'realtime', ticks: { source: 'data' } },
                        y: {
                            beginAtZero: true,
                            grid: {
                                // Hide zero line and maximum line
                                color: context =>
                                    context.tick.value === 0 ||
                                    context.tick.value === context.chart.scales.y.max
                                        ? 'transparent'
                                        : 'rgba(234, 234, 234, 1)',
                            },
                            ticks: { maxTicksLimit: 3 },
                        },
                    },
                    plugins: {
                        streaming: this.streamingOpt,
                        tooltip: {
                            external: context =>
                                streamTooltip({ context, tooltipId: scope.uniqueTooltipId }),
                        },
                    },
                },
                this.baseOpts
            )
        },
    },
    watch: {
        refreshRate: {
            immediate: true,
            handler(v, oV) {
                /**
                 * Handle pause/resume stream in the next tick for the initial render,
                 * because chartInstance is only available in the nextTick
                 */
                if (this.$typy(oV).isUndefined) this.$nextTick(() => this.handleStream())
                else this.handleStream()
            },
        },
    },
    beforeDestroy() {
        let tooltipEl = document.getElementById(this.uniqueTooltipId)
        if (tooltipEl) tooltipEl.remove()
    },
    methods: {
        handleStream() {
            this.$set(this.streamingOpt, 'pause', this.isPaused)
            this.$set(
                this.streamingOpt,
                'delay',
                this.isPaused ? this.streamingOpt.delay : (this.refreshRate + 1) * 1000
            )
        },
    },
}
</script>
