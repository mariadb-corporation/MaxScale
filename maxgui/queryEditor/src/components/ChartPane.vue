<template>
    <div class="chart-pane fill-height">
        <div ref="chartTool" class="d-flex pt-2 pr-3">
            <v-spacer />
            <mxs-tooltip-btn small icon depressed color="accent-dark" @click="exportToJpeg">
                <template v-slot:btn-content>
                    <v-icon size="16" color="accent-dark"> mdi-download </v-icon>
                </template>
                {{ $mxs_t('exportChart') }}
            </mxs-tooltip-btn>
            <mxs-tooltip-btn
                small
                icon
                depressed
                @click="chartOpt.isMaximized = !chartOpt.isMaximized"
            >
                <template v-slot:btn-content>
                    <v-icon size="18" color="accent-dark">
                        mdi-fullscreen{{ chartOpt.isMaximized ? '-exit' : '' }}
                    </v-icon>
                </template>
                {{ chartOpt.isMaximized ? $mxs_t('minimize') : $mxs_t('maximize') }}
            </mxs-tooltip-btn>
            <mxs-tooltip-btn
                btnClass="close-chart"
                small
                icon
                depressed
                color="accent-dark"
                @click="$emit('close-chart')"
            >
                <template v-slot:btn-content>
                    <v-icon size="12" color="accent-dark"> $vuetify.icons.mxs_close</v-icon>
                </template>
                {{ $mxs_t('close') }}
            </mxs-tooltip-btn>
        </div>
        <div ref="chartWrapper" :key="chartHeight" class="chart-wrapper">
            <mxs-line-chart
                v-if="type === chartTypes.LINE"
                id="query-chart"
                :style="chartStyle"
                hasVertCrossHair
                :chartData="chartData"
                :opts="chartOptions"
            />
            <mxs-scatter-chart
                v-else-if="type === chartTypes.SCATTER"
                id="query-chart"
                :style="chartStyle"
                :chartData="chartData"
                :opts="chartOptions"
            />
            <mxs-vert-bar-chart
                v-else-if="type === chartTypes.BAR_VERT"
                id="query-chart"
                :style="chartStyle"
                :chartData="chartData"
                :opts="chartOptions"
            />
            <mxs-horiz-bar-chart
                v-else-if="type === chartTypes.BAR_HORIZ"
                id="query-chart"
                :style="chartStyle"
                :chartData="chartData"
                :opts="chartOptions"
            />
        </div>
    </div>
</template>

<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
/*
@close-chart. Emit when close-chart button is clicked
*/
import { objectTooltip } from '@share/components/common/MxsCharts/customTooltips.js'
export default {
    name: 'chart-pane',
    props: {
        value: { type: Object, required: true },
        containerHeight: { type: Number, default: 0 },
        chartTypes: { type: Object, required: true }, // SQL_CHART_TYPES object
        axisTypes: { type: Object, required: true }, // SQL_CHART_AXIS_TYPES object
    },
    data() {
        return {
            uniqueTooltipId: this.$helpers.lodash.uniqueId('tooltip_'),
            dataPoint: null,
            chartToolHeight: 0,
        }
    },
    computed: {
        chartOpt: {
            get() {
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
        chartData() {
            return this.chartOpt.data
        },
        chartStyle() {
            return {
                minHeight: `${this.chartHeight}px`,
                minWidth: this.minWidth,
            }
        },
        axesType() {
            return this.chartOpt.axesType
        },
        scaleLabels() {
            return this.chartOpt.scaleLabels
        },
        type() {
            return this.chartOpt.type
        },
        minWidth() {
            if (this.autoSkipTick(this.axesType.x) || this.type === this.chartTypes.BAR_HORIZ)
                return 'unset'
            if (this.$typy(this.chartData, 'xLabels').isDefined)
                return `${Math.min(this.chartData.xLabels.length * 15, 15000)}px`
            return '0px'
        },
        chartHeight() {
            if (this.autoSkipTick(this.axesType.y))
                return this.containerHeight - (this.chartToolHeight + 12)
            /** When there is too many data points,
             * first, get min value between "overflow" height (this.chartData.yLabels.length * 15)
             * and max height threshold 15000. However, when there is too little data points,
             * the "overflow" height is smaller than container height, container height
             * should be chosen to make chart fit to its container
             */
            return Math.max(
                this.containerHeight - (this.chartToolHeight + 12),
                Math.min(this.$typy(this.chartData, 'yLabels').safeArray.length * 15, 15000)
            )
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
                    mode: 'nearest',
                    onHover: (e, el) => {
                        e.target.style.cursor = el[0] ? 'pointer' : 'default'
                    },
                    animationDuration: 0,
                    intersect: false,
                },
                tooltips: {
                    mode: 'nearest',
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
                            type: this.axesType.x,
                            scaleLabel: {
                                display: true,
                                labelString: this.scaleLabels.x,
                                fontSize: 14,
                                lineHeight: 1,
                                padding: {
                                    top: 16,
                                },
                                fontColor: '#424f62',
                            },
                            ticks: this.getAxisTicks({ axisId: 'x', axisType: this.axesType.x }),
                        },
                    ],
                    yAxes: [
                        {
                            type: this.axesType.y,
                            scaleLabel: {
                                display: true,
                                labelString: this.scaleLabels.y,
                                fontSize: 14,
                                padding: {
                                    bottom: 16,
                                },
                                fontColor: '#424f62',
                            },
                            ticks: this.getAxisTicks({ axisId: 'y', axisType: this.axesType.y }),
                        },
                    ],
                },
            }
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
        /**
         * Check if provided axisType is either LINEAR OR TIME type.
         * @param {String} param.axisType - SQL_CHART_AXIS_TYPES
         * @returns {Boolean} - should autoSkip the tick
         */
        autoSkipTick(axisType) {
            const { LINEAR, TIME } = this.axisTypes
            return axisType === LINEAR || axisType === TIME
        },
        /**
         * Get the ticks object
         * @param {String} param.axisType - SQL_CHART_AXIS_TYPES
         * @param {String} param.axisId- x or y
         * @returns {Object} - ticks object
         */
        getAxisTicks({ axisId, axisType }) {
            const { CATEGORY } = this.axisTypes
            const { LINE, SCATTER, BAR_VERT, BAR_HORIZ } = this.chartTypes
            const autoSkip = this.autoSkipTick(this.axesType[axisType])
            let ticks = { autoSkip, callback: this.truncateLabel, beginAtZero: true }
            if (autoSkip) {
                ticks.autoSkipPadding = 15
            }
            switch (this.type) {
                case LINE:
                case SCATTER:
                case BAR_VERT:
                case BAR_HORIZ:
                    // only rotate tick label for the X axis and CATEGORY axis type
                    if (axisId === 'x' && axisType === CATEGORY) {
                        ticks.maxRotation = this.autoSkipTick(this.axesType[axisType]) ? 0 : 90
                        ticks.minRotation = this.autoSkipTick(this.axesType[axisType]) ? 0 : 90
                    }
                    break
            }
            return ticks
        },
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
            return `MaxScale ${this.type} Chart - ${this.$helpers.dateFormat({
                moment: this.$moment,
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
.chart-pane {
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
