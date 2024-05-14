<template>
    <div class="chart-pane fill-height">
        <div ref="chartTool" class="d-flex pt-2 pr-3">
            <v-spacer />
            <mxs-tooltip-btn small icon depressed color="primary" @click="exportChart">
                <template v-slot:btn-content>
                    <v-icon size="16"> mdi-download </v-icon>
                </template>
                {{ $mxs_t('exportChart') }}
            </mxs-tooltip-btn>
            <mxs-tooltip-btn
                small
                icon
                depressed
                color="primary"
                @click="chartOpt.isMaximized = !chartOpt.isMaximized"
            >
                <template v-slot:btn-content>
                    <v-icon size="18">
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
                color="primary"
                @click="$emit('close-chart')"
            >
                <template v-slot:btn-content>
                    <v-icon size="12"> $vuetify.icons.mxs_close</v-icon>
                </template>
                {{ $mxs_t('close') }}
            </mxs-tooltip-btn>
        </div>
        <!-- Chart height will be calculated accurately only after chart tool is fully rendered. -->
        <div v-if="chartToolHeight" ref="chartWrapper" class="chart-wrapper">
            <mxs-line-chart
                v-if="type === chartTypes.LINE"
                ref="chart"
                :style="chartStyle"
                hasVertCrossHair
                :chartData="chartData"
                :opts="chartOptions"
            />
            <mxs-scatter-chart
                v-else-if="type === chartTypes.SCATTER"
                ref="chart"
                :style="chartStyle"
                :chartData="chartData"
                :opts="chartOptions"
            />
            <mxs-bar-chart
                v-else-if="type === chartTypes.BAR_VERT || type === chartTypes.BAR_HORIZ"
                ref="chart"
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
 * Change Date: 2027-04-10
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
        axisTypes: { type: Object, required: true }, // CHART_AXIS_TYPES object
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
        tableData() {
            return this.chartOpt.tableData
        },
        chartData() {
            return this.chartOpt.chartData
        },
        labels() {
            return this.chartData.labels
        },
        chartStyle() {
            return {
                height: this.chartHeight,
                minWidth: this.chartWidth,
            }
        },
        axesType() {
            return this.chartOpt.axesType
        },
        axisKeys() {
            return this.chartOpt.axisKeys
        },
        type() {
            return this.chartOpt.type
        },
        hasTrendline() {
            return this.chartOpt.hasTrendline
        },
        chartWidth() {
            if (this.autoSkipTick(this.axesType.x)) return 'unset'
            return `${Math.min(this.labels.length * 15, 15000)}px`
        },
        chartHeight() {
            let height = this.containerHeight - (this.chartToolHeight + 12)
            if (!this.autoSkipTick(this.axesType.y))
                /** When there is too many data points,
                 * first, get min value between "overflow" height (this.labels.length * 15)
                 * and max height threshold 15000. However, when there is too little data points,
                 * the "overflow" height is smaller than container height, container height
                 * should be chosen to make chart fit to its container
                 */
                height = Math.max(height, Math.min(this.labels.length * 15, 15000))
            return `${height}px`
        },
        chartOptions() {
            const scope = this
            let options = {
                layout: { padding: { left: 12, bottom: 12, right: 24, top: 24 } },
                animation: { active: { duration: 0 } },
                onHover: (e, el) => {
                    e.native.target.style.cursor = el[0] ? 'pointer' : 'default'
                },
                scales: {
                    x: {
                        type: this.axesType.x,
                        title: {
                            display: true,
                            text: this.axisKeys.x,
                            font: { size: 14 },
                            padding: { top: 16 },
                            color: '#424f62',
                        },
                        beginAtZero: true,
                        ticks: this.getAxisTicks({ axisId: 'x', axisType: this.axesType.x }),
                    },
                    y: {
                        type: this.axesType.y,
                        title: {
                            display: true,
                            text: this.axisKeys.y,
                            font: { size: 14 },
                            padding: { bottom: 16 },
                            color: '#424f62',
                        },
                        beginAtZero: true,
                        ticks: this.getAxisTicks({ axisId: 'y', axisType: this.axesType.y }),
                    },
                },
                plugins: {
                    tooltip: {
                        callbacks: {
                            label(context) {
                                scope.dataPoint = scope.tableData[context.dataIndex]
                            },
                        },
                        external: context =>
                            objectTooltip({
                                context,
                                tooltipId: scope.uniqueTooltipId,
                                dataPoint: scope.dataPoint,
                                axisKeys: scope.axisKeys,
                                alignTooltipToLeft:
                                    context.tooltip.caretX >=
                                    scope.$refs.chartWrapper.clientWidth / 2,
                            }),
                    },
                },
            }
            if (this.chartOpt.isHorizChart) options.indexAxis = 'y'
            return options
        },
    },
    watch: {
        chartData(v) {
            if (!this.$typy(v, 'datasets[0].data[0]').safeObject) this.removeTooltip()
        },
        /**
         * A workaround for toggling the trendline.
         * There is a bug in vue-chartjs v4: https://github.com/apertureless/vue-chartjs/issues/1006
         */
        hasTrendline(v) {
            let dataset = this.$typy(this.$refs, 'chart.chartInstance.data.datasets[0]')
                .safeObjectOrEmpty
            if (v)
                dataset.trendlineLinear = {
                    colorMin: '#2d9cdb',
                    colorMax: '#2d9cdb',
                    lineStyle: 'solid',
                    width: 2,
                }
            else delete dataset.trendlineLinear
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
         * @param {String} param.axisType - CHART_AXIS_TYPES
         * @returns {Boolean} - should autoSkip the tick
         */
        autoSkipTick(axisType) {
            const { LINEAR, TIME } = this.axisTypes
            return axisType === LINEAR || axisType === TIME
        },
        /**
         * Get the ticks object
         * @param {String} param.axisType - CHART_AXIS_TYPES
         * @param {String} param.axisId- x or y
         * @returns {Object} - ticks object
         */
        getAxisTicks({ axisId, axisType }) {
            const scope = this
            const { CATEGORY } = this.axisTypes
            const autoSkip = this.autoSkipTick(this.axesType[axisType])
            let ticks = {
                autoSkip,
                callback: function(value) {
                    const v = this.getLabelForValue(value)
                    if (scope.$typy(v).isString && v.length > 10) return `${v.substr(0, 10)}...`
                    return v
                },
            }
            if (autoSkip) ticks.autoSkipPadding = 15
            // only rotate tick label for the X axis and CATEGORY axis type
            if (axisId === 'x' && axisType === CATEGORY) {
                ticks.maxRotation = 90
                ticks.minRotation = 90
            }
            return ticks
        },
        removeTooltip() {
            let tooltipEl = document.getElementById(this.uniqueTooltipId)
            if (tooltipEl) tooltipEl.remove()
        },

        getDefFileName() {
            return `MaxScale ${this.type} Chart - ${this.$helpers.dateFormat({
                value: new Date(),
            })}`
        },
        exportChart() {
            const chart = this.$refs.chart.$el
            const canvas = chart.getElementsByTagName('canvas')[0]
            this.$helpers.exportToJpeg({ canvas, fileName: this.getDefFileName() })
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
