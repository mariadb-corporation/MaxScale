<template>
    <div v-if="!$typy(chartData, 'datasets').isEmptyArray" class="chart-container fill-height">
        <div ref="chartTool" class="d-flex pt-2 pr-3">
            <v-spacer />
            <v-tooltip
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
            >
                <template v-slot:activator="{ on }">
                    <v-btn
                        x-small
                        text
                        depressed
                        color="accent-dark"
                        v-on="on"
                        @click="exportToJpeg"
                    >
                        <v-icon size="16" color="accent-dark">
                            file_download
                        </v-icon>
                    </v-btn>
                </template>
                <span>{{ $t('exportChart') }}</span>
            </v-tooltip>
            <v-tooltip
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
            >
                <template v-slot:activator="{ on }">
                    <v-btn
                        x-small
                        text
                        depressed
                        v-on="on"
                        @click="$emit('is-chart-maximized', !isChartMaximized)"
                    >
                        <v-icon size="18" color="accent-dark">
                            fullscreen{{ isChartMaximized ? '_exit' : '' }}
                        </v-icon>
                    </v-btn>
                </template>
                <span>{{ isChartMaximized ? $t('minimize') : $t('maximize') }}</span>
            </v-tooltip>
        </div>

        <div ref="chartWrapper" :key="chartHeight" class="chart-wrapper">
            <line-chart
                v-if="selectedChart === 'Line'"
                id="query-chart"
                class="line-chart"
                :style="{
                    minHeight: `${chartHeight}px`,
                    minWidth: minWidth,
                }"
                hasVertCrossHair
                :chartData="chartData"
                :options="lineChartOptions"
            />
            <scatter-chart
                v-else-if="selectedChart === 'Scatter'"
                id="query-chart"
                class="scatter-chart"
                :style="{
                    minHeight: `${chartHeight}px`,
                    minWidth: minWidth,
                }"
                :chartData="chartData"
                :options="scatterChartOptions"
            />
            <vert-bar-chart
                v-else-if="selectedChart === 'Bar - Vertical'"
                id="query-chart"
                class="vert-bar-chart"
                :style="{
                    minHeight: `${chartHeight}px`,
                    minWidth: minWidth,
                }"
                :chartData="chartData"
                :options="vertBarChartOptions"
            />
            <horiz-bar-chart
                v-else-if="selectedChart === 'Bar - Horizontal'"
                id="query-chart"
                class="vert-bar-chart"
                :style="{
                    minHeight: `${chartHeight}px`,
                    minWidth: 'unset',
                }"
                :chartData="chartData"
                :options="horizBarChartOptions"
            />
        </div>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-02-11
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
        containerChartHeight: { type: Number, default: 0 },
        chartData: { type: Object, default: () => {} },
        axisLabels: { type: Object, default: () => {} },
        xAxisType: { type: String, required: true },
        isChartMaximized: { type: Boolean, required: true },
    },
    data() {
        return {
            uniqueTooltipId: this.$help.lodash.uniqueId('tooltip_'),
            dataPoint: null,
            chartToolHeight: 0,
        }
    },
    computed: {
        isTimeChart() {
            return this.xAxisType === 'time'
        },
        isLinear() {
            return this.xAxisType === 'linear'
        },
        autoSkipXTick() {
            return this.isLinear || this.isTimeChart
        },
        minWidth() {
            if (this.autoSkipXTick) return 'unset'
            if (this.$typy(this.chartData, 'labels').isDefined)
                return `${Math.min(this.chartData.labels.length * 15, 15000)}px`
            return '0px'
        },
        chartHeight() {
            switch (this.selectedChart) {
                case 'Bar - Horizontal':
                    if (this.autoSkipXTick)
                        return this.containerChartHeight - (this.chartToolHeight + 12)
                    /** When there is too many data points,
                     * first, get min value between "overflow" height (this.chartData.labels.length * 15)
                     * and max height threshold 15000. However, when there is too little data points,
                     * the "overflow" height is smaller than container height, container height
                     * should be chosen to make chart fit to its container
                     */
                    return Math.max(
                        this.containerChartHeight - (this.chartToolHeight + 12),
                        Math.min(this.chartData.labels.length * 15, 15000)
                    )
                default:
                    // 10px of scrollbar height plus border
                    return this.containerChartHeight - (this.chartToolHeight + 12)
            }
        },
        chartOptions() {
            const componentScope = this
            return {
                layout: {
                    padding: {
                        left: 12,
                        bottom: 12,
                        right: 24,
                        top: 24,
                    },
                },
                responsive: true,
                maintainAspectRatio: false,
                hover: {
                    onHover: (e, el) => {
                        e.target.style.cursor = el[0] ? 'pointer' : 'default'
                    },
                    animationDuration: 0,
                    intersect: false,
                },
                tooltips: {
                    enabled: false,
                    intersect: false,
                    position: 'cursor',
                    custom: function(tooltipModel) {
                        const chartScope = this
                        const position = chartScope._chart.canvas.getBoundingClientRect()
                        objectTooltip({
                            tooltipModel,
                            tooltipId: componentScope.uniqueTooltipId,
                            position,
                            dataPoint: componentScope.dataPoint,
                            alignTooltipToLeft:
                                tooltipModel.caretX >=
                                componentScope.$refs.chartWrapper.clientWidth / 2,
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
                                padding: {
                                    top: 16,
                                },
                                fontColor: '#424f62',
                            },
                            beginAtZero: true,
                        },
                    ],
                    yAxes: [
                        {
                            scaleLabel: {
                                display: true,
                                labelString: this.axisLabels.y,
                                fontSize: 14,
                                padding: {
                                    bottom: 16,
                                },
                            },
                            ticks: {
                                beginAtZero: true,
                            },
                        },
                    ],
                },
            }
        },
        lineChartOptions() {
            let lineOptions = {
                showLines: true,
                hover: {
                    mode: this.isLinear ? 'nearest' : 'index',
                },
                tooltips: {
                    mode: this.isLinear ? 'nearest' : 'index',
                },
                scales: {
                    xAxes: [
                        {
                            type: this.xAxisType,
                            bounds: 'ticks',
                            ticks: {
                                autoSkip: this.autoSkipXTick,
                                autoSkipPadding: 15,
                                maxRotation: this.autoSkipXTick ? 0 : 90,
                                minRotation: this.autoSkipXTick ? 0 : 90,
                                //truncate tick
                                callback: this.truncateLabel,
                            },
                        },
                    ],
                },
            }
            return this.$help.lodash.deepMerge(this.chartOptions, lineOptions)
        },
        scatterChartOptions() {
            return this.$help.lodash.deepMerge(this.chartOptions, {
                hover: {
                    mode: 'nearest',
                },
                tooltips: {
                    mode: 'nearest',
                },
                scales: {
                    xAxes: [
                        {
                            type: this.xAxisType,
                            bounds: 'ticks',
                            ticks: {
                                autoSkip: this.autoSkipXTick,
                                autoSkipPadding: 15,
                                maxRotation: this.autoSkipXTick ? 0 : 90,
                                minRotation: this.autoSkipXTick ? 0 : 90,
                            },
                        },
                    ],
                },
            })
        },
        vertBarChartOptions() {
            return this.$help.lodash.deepMerge(this.chartOptions, {
                hover: {
                    mode: 'index',
                },
                tooltips: {
                    mode: 'index',
                },
                scales: {
                    xAxes: [
                        {
                            ticks: {
                                autoSkip: this.autoSkipXTick,
                                autoSkipPadding: 15,
                                maxRotation: this.autoSkipXTick ? 0 : 90,
                                minRotation: this.autoSkipXTick ? 0 : 90,
                                callback: this.truncateLabel,
                            },
                        },
                    ],
                },
            })
        },
        horizBarChartOptions() {
            return this.$help.lodash.deepMerge(this.chartOptions, {
                hover: {
                    mode: 'index',
                },
                tooltips: {
                    mode: 'index',
                },
                scales: {
                    yAxes: [
                        {
                            ticks: {
                                autoSkip: this.autoSkipXTick,
                                autoSkipPadding: 15,
                                callback: this.truncateLabel,
                                reverse: true,
                            },
                        },
                    ],
                },
            })
        },
    },
    watch: {
        chartData(v) {
            if (!this.$typy(v, 'datasets[0].data[0]').safeObject) this.removeTooltip()
        },
    },
    mounted() {
        this.$nextTick(() => {
            this.chartToolHeight = this.$refs.chartTool.offsetHeight
        })
    },
    beforeDestroy() {
        this.removeTooltip()
    },
    methods: {
        removeTooltip() {
            let tooltipEl = document.getElementById(this.uniqueTooltipId)
            if (tooltipEl) tooltipEl.remove()
        },
        truncateLabel(v) {
            const toStr = `${v}`
            if (toStr.length > 10) return `${toStr.substr(0, 10)}...`
            return v
        },
        getDefFileName() {
            return `MaxScale ${this.selectedChart} Chart - ${this.$help.dateFormat({
                value: new Date(),
                formatType: 'DATE_RFC2822',
            })}`
        },
        createCanvasFrame() {
            const chart = document.querySelector('#query-chart')
            const srcCanvas = chart.getElementsByTagName('canvas')[0]

            // create new canvas with white background
            let desCanvas = document.createElement('canvas')

            desCanvas.width = srcCanvas.width
            desCanvas.height = srcCanvas.height

            let destCtx = desCanvas.getContext('2d')

            destCtx.fillStyle = '#FFFFFF'
            destCtx.fillRect(0, 0, desCanvas.width, desCanvas.height)

            //draw the original canvas onto the destination canvas
            destCtx.drawImage(srcCanvas, 0, 0)
            destCtx.scale(2, 2)
            return desCanvas
        },
        exportToJpeg() {
            const desCanvas = this.createCanvasFrame()
            const imageUrl = desCanvas.toDataURL('image/jpeg', 1.0)
            let a = document.createElement('a')
            a.href = imageUrl
            a.download = `${this.getDefFileName()}.jpeg`
            document.body.appendChild(a)
            a.click()
            document.body.removeChild(a)
        },
    },
}
</script>
<style lang="scss" scoped>
.chart-container {
    overflow: auto;
    .chart-wrapper {
        width: 100%;
        overflow: auto;
        canvas {
            position: absolute;
            left: 0;
            top: 0;
        }
    }
}
</style>
