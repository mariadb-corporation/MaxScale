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
        <scatter-chart
            v-else-if="selectedChart === 'Scatter'"
            class="scatter-chart-container py-2 px-3"
            :style="{
                minWidth: minLineChartWidth,
                minHeight: `${chartHeight}px`,
            }"
            hasVertCrossHair
            :chartData="chartData"
            :options="scatterChartOptions"
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
            if (this.$typy(this.chartData, 'labels').isDefine)
                return `${Math.min(this.chartData.labels.length * 15, 15000)}px`
            return '0px'
        },
        getXAxisDataType() {
            if (this.$typy(this.chartData, 'datasets[0].data[0]').safeObject)
                return typeof this.chartData.datasets[0].data[0].x
            return null
        },
        chartOptions() {
            const componentScope = this
            return {
                responsive: true,
                spanGaps: true,
                maintainAspectRatio: false,
                hover: {
                    onHover: e => this.onChartHover(e),
                },
                tooltips: {
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
        lineChartOptions() {
            const isNum = this.getXAxisDataType === 'number'
            let lineOptions = {
                showLines: true,
                hover: {
                    mode: 'index',
                    intersect: false,
                },
                tooltips: {
                    mode: 'index',
                    position: 'cursor',
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
                },
            }
            return this.$help.lodash.deepMerge(this.chartOptions, lineOptions)
        },
        scatterChartOptions() {
            return this.$help.lodash.deepMerge(this.chartOptions, {
                tooltips: {
                    position: 'cursor',
                    intersect: false,
                },
            })
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
