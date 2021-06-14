<template>
    <div
        v-if="!$typy(sortedChartData, 'datasets').isEmptyArray"
        ref="chartContainer"
        class="chart-container fill-height"
    >
        <div class="d-flex pa-2 pr-5">
            <v-spacer />
            <v-tooltip
                top
                transition="slide-y-transition"
                content-class="shadow-drop color text-navigation py-1 px-4"
            >
                <template v-slot:activator="{ on }">
                    <v-btn
                        x-small
                        outlined
                        depressed
                        color="accent-dark"
                        v-on="on"
                        @click="exportToJpeg"
                    >
                        <v-icon size="14" color="accent-dark">
                            file_download
                        </v-icon>
                    </v-btn>
                </template>
                <span>{{ $t('exportChart') }}</span>
            </v-tooltip>
        </div>

        <line-chart
            v-if="selectedChart === 'Line'"
            id="query-chart"
            class="line-chart-container pa-3"
            :style="{
                minHeight: `${chartHeight}px`,
                minWidth,
            }"
            hasVertCrossHair
            :chartData="sortedChartData"
            :options="lineChartOptions"
        />
        <scatter-chart
            v-else-if="selectedChart === 'Scatter'"
            id="query-chart"
            class="scatter-chart-container pa-3"
            :style="{
                minHeight: `${chartHeight}px`,
                minWidth,
            }"
            hasVertCrossHair
            :chartData="sortedChartData"
            :options="scatterChartOptions"
        />
        <vert-bar-chart
            v-else-if="selectedChart === 'Bar - Vertical'"
            id="query-chart"
            class="vert-bar-chart-container pa-3"
            :style="{
                minHeight: `${chartHeight}px`,
                minWidth,
            }"
            :chartData="sortedChartData"
            :options="barChartOptions"
        />
        <horiz-bar-chart
            v-else-if="selectedChart === 'Bar - Horizontal'"
            id="query-chart"
            class="vert-bar-chart-container pa-3"
            :style="{
                minHeight: `${chartHeight}px`,
                minWidth,
            }"
            :chartData="sortedChartData"
            :options="barChartOptions"
        />
        <!-- TODO: Addfullscreen mode feat-->
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
        containerChartHeight: { type: Number, default: 0 },
        chartData: { type: Object, default: () => {} },
        axisLabels: { type: Object, default: () => {} },
    },
    data() {
        return {
            uniqueTooltipId: this.$help.lodash.uniqueId('tooltip_'),
            dataPoint: null,
        }
    },
    computed: {
        minWidth() {
            if (this.isLinear) return 'unset'
            if (this.$typy(this.chartData, 'labels').isDefined)
                return `${Math.min(this.chartData.labels.length * 15, 15000)}px`
            return '0px'
        },
        chartHeight() {
            switch (this.selectedChart) {
                case 'Bar - Horizontal':
                    /** When there is too many data points,
                     * first, get min value between "overflow" height (this.chartData.labels.length * 15)
                     * and max height threshold 15000. However, when there is too little data points,
                     * the "overflow" height is smaller than container height, container height
                     * should be chosen to make chart fit to its container
                     */
                    return Math.max(
                        this.containerChartHeight - 36,
                        Math.min(this.chartData.labels.length * 15, 15000)
                    )
                default:
                    return this.containerChartHeight - 36 // export button height
            }
        },
        isLinear() {
            if (this.$typy(this.chartData, 'datasets[0].data[0]').safeObject)
                return typeof this.chartData.datasets[0].data[0].x === 'number'
            return false
        },
        sortedChartData() {
            if (this.isLinear) {
                let chartData = this.$help.lodash.cloneDeep(this.chartData)
                chartData.labels.sort((a, b) => a - b)
                chartData.datasets[0].data.sort((a, b) => {
                    if (a.x < b.x) return -1
                    if (a.x > b.x) return 1
                    return 0
                })
                return chartData
            }
            return this.chartData
        },
        chartOptions() {
            const componentScope = this
            return {
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
                                componentScope.$refs.chartContainer.clientWidth / 2,
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
                        },
                    ],
                },
            }
        },
        cartesianAxes() {
            return {
                type: this.isLinear ? 'linear' : 'category',
                ticks: {
                    autoSkip: this.isLinear,
                    autoSkipPadding: this.isLinear ? 0 : 15,
                    maxRotation: this.isLinear ? 0 : 90,
                    minRotation: this.isLinear ? 0 : 90,
                    //truncate tick
                    callback: v => {
                        const toStr = `${v}`
                        if (toStr.length > 10) return `${toStr.substr(0, 10)}...`
                        return v
                    },
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
                    xAxes: [this.cartesianAxes],
                },
            }
            return this.$help.lodash.deepMerge(this.chartOptions, lineOptions)
        },
        scatterChartOptions() {
            return this.chartOptions
        },
        barChartOptions() {
            return this.$help.lodash.deepMerge(this.chartOptions, {
                hover: {
                    mode: 'index',
                },
                tooltips: {
                    mode: 'index',
                },
                scales: {
                    xAxes: [this.cartesianAxes],
                },
            })
        },
    },
    watch: {
        chartData(v) {
            if (!this.$typy(v, 'datasets[0].data[0]').safeObject) this.removeTooltip()
        },
    },
    beforeDestroy() {
        this.removeTooltip()
    },
    methods: {
        removeTooltip() {
            let tooltipEl = document.getElementById(this.uniqueTooltipId)
            if (tooltipEl) tooltipEl.remove()
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

            // set des canvas plus extra padding
            desCanvas.width = srcCanvas.width + 24
            desCanvas.height = srcCanvas.height + 36

            let destCtx = desCanvas.getContext('2d')

            destCtx.fillStyle = '#FFFFFF'
            destCtx.fillRect(0, 0, desCanvas.width, desCanvas.height)

            //draw the original canvas onto the destination canvas
            destCtx.drawImage(srcCanvas, 12, 24) // center srcCanvas
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
    width: 100%;
    overflow: auto;
    canvas {
        position: absolute;
        left: 0;
        top: 0;
    }
}
</style>
