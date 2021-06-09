<template>
    <div
        v-if="!$typy(chartData, 'datasets').isEmptyArray"
        ref="chartContainer"
        class="chart-container fill-height"
    >
        <line-chart
            v-if="selectedChart === 'Line'"
            class="line-chart-container py-2 px-3"
            :style="{
                minWidth: minLineChartWidth,
                minHeight: `${chartHeight}px`,
            }"
            hasVertCrossHair
            :chartData="chartData"
            :options="lineChartOptions"
        />
        <!-- TODO: Add more charts, add fullscreen mode and export chart feat-->
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { objectTooltip } from '@/components/common/Charts/customTooltips.js'
export default {
    name: 'chart-container',
    props: {
        selectedChart: { type: String, default: '' },
        chartHeight: { type: Number, default: 0 },
        chartData: { type: Object, default: () => {} },
        axisLabels: { type: Object, default: () => {} },
    },
    data() {
        return {
            uniqueTooltipId: this.$help.lodash.uniqueId('tooltip_'),
            alignTooltipToLeft: false,
            dataPoint: null,
        }
    },
    computed: {
        minLineChartWidth() {
            return `${Math.min(this.chartData.labels.length * 15, 15000)}px`
        },
        getXAxisDataType() {
            if (this.$typy(this.chartData, 'datasets[0].data[0]').safeObject)
                return typeof this.chartData.datasets[0].data[0].x
            return null
        },
        lineChartOptions() {
            const isNum = this.getXAxisDataType === 'number'
            const componentScope = this
            return {
                showLines: true,
                responsive: true,
                spanGaps: true,
                maintainAspectRatio: false,
                hover: {
                    mode: 'index',
                    intersect: false,
                    onHover: e => this.onChartHover(e),
                },
                tooltips: {
                    mode: 'index',
                    intersect: false,
                    enabled: false,
                    custom: function(tooltipModel) {
                        const chartScope = this
                        const position = chartScope._chart.canvas.getBoundingClientRect()
                        objectTooltip({
                            tooltipModel,
                            tooltipId: componentScope.uniqueTooltipId,
                            position,
                            dataPoint: componentScope.dataPoint,
                            alignTooltipToLeft: componentScope.alignTooltipToLeft,
                        })
                    },
                    callbacks: {
                        label(tooltipItem, data) {
                            componentScope.dataPoint =
                                data.datasets[tooltipItem.datasetIndex].data[tooltipItem.index]
                        },
                    },
                },
                legend: {
                    display: false,
                },
                elements: {
                    point: {
                        radius: 0,
                    },
                },
                scales: {
                    xAxes: [
                        {
                            scaleLabel: {
                                display: true,
                                labelString: this.axisLabels.x,
                                fontSize: 14,
                                lineHeight: 1,
                                padding: 0,
                                fontColor: '#424f62',
                            },
                            ticks: {
                                maxRotation: isNum ? 0 : 90,
                                minRotation: isNum ? 0 : 90,
                                //truncate tick
                                callback: v => {
                                    const toStr = `${v}`
                                    if (toStr.length > 10) return `${toStr.substr(0, 10)}...`
                                    return v
                                },
                            },
                        },
                    ],
                    yAxes: [
                        {
                            scaleLabel: {
                                display: true,
                                labelString: this.axisLabels.y,
                                fontSize: 14,
                                padding: 16,
                            },
                        },
                    ],
                },
            }
        },
    },
    beforeDestroy() {
        let tooltipEl = document.getElementById(this.uniqueTooltipId)
        if (tooltipEl) tooltipEl.remove()
    },
    methods: {
        onChartHover(e) {
            this.alignTooltipToLeft = e.offsetX >= this.$refs.chartContainer.clientWidth / 2
        },
    },
}
</script>
<style lang="scss" scoped>
.chart-container {
    width: 100%;
    overflow: auto;
    canvas {
        position: absolute;
        left: 0;
        top: 0;
    }
}
</style>
