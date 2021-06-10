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
            }"
            hasVertCrossHair
            :chartData="sortedChartData"
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
        containerChartHeight: { type: Number, default: 0 },
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
        chartHeight() {
            return this.containerChartHeight - 36 // export button height
        },
        isLinear() {
            if (this.$typy(this.chartData, 'datasets[0].data[0]').safeObject)
                return typeof this.chartData.datasets[0].data[0].x === 'number'
            return null
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
                        this.onChartHover(e)
                        e.target.style.cursor = el[0] ? 'pointer' : 'default'
                    },
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
        lineChartOptions() {
            let lineOptions = {
                showLines: true,
                hover: {
                    mode: this.isLinear ? 'nearest' : 'index',
                    intersect: false,
                },
                tooltips: {
                    mode: this.isLinear ? 'nearest' : 'index',
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
                            type: this.isLinear ? 'linear' : 'category',
                            ticks: {
                                autoSkip: !this.isLinear,
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
