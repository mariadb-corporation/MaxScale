<template>
    <div class="pa-4">
        <h5 class="mb-4">{{ $t('visualization') }}</h5>
        <label class="field__label color text-small-text"> {{ $t('graph') }}</label>
        <v-select
            v-model="selectedChart"
            :items="chartTypes"
            outlined
            class="std mariadb-select-input error--text__bottom"
            :menu-props="{
                contentClass: 'mariadb-select-v-menu',
                bottom: true,
                offsetY: true,
            }"
            dense
            :height="36"
            hide-details="auto"
        />
        <div v-if="selectedChart !== $t('noVisualization')" class="mt-4">
            <label class="field__label color text-small-text"> {{ $t('selectResultSet') }}</label>
            <v-select
                v-model="resSet"
                :items="resultSets"
                outlined
                class="std mariadb-select-input error--text__bottom"
                :menu-props="{
                    contentClass: 'mariadb-select-v-menu',
                    bottom: true,
                    offsetY: true,
                }"
                dense
                :height="36"
                hide-details="auto"
                item-text="id"
                item-value="id"
                return-object
            />
            <template v-if="resSet">
                <!-- Don't show axis inputs if result set is empty -->
                <div v-if="$typy(resSet, 'data').isEmptyArray" class="mt-4 color text-small-text">
                    {{ $t('emptySet') }}
                </div>
                <template v-for="a in ['x', 'y']" v-else>
                    <div :key="a" class="mt-2">
                        <label class="field__label color text-small-text text-capitalize">
                            {{ a }} axis
                        </label>
                        <v-select
                            v-model="axis[a]"
                            :items="a === 'y' ? yAxisFields : xAxisFields"
                            outlined
                            class="std mariadb-select-input error--text__bottom"
                            :menu-props="{
                                contentClass: 'mariadb-select-v-menu',
                                bottom: true,
                                offsetY: true,
                            }"
                            dense
                            :height="36"
                            hide-details="auto"
                        />
                    </div>
                </template>
                <v-checkbox
                    v-if="supportTrendLine"
                    v-model="showTrendline"
                    dense
                    color="primary"
                    class="mt-2 show-trendline"
                    hide-details
                    :label="$t('showTrendline')"
                />
            </template>
            <!-- TODO: add more graph configurations -->
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
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapGetters, mapState } from 'vuex'
export default {
    name: 'visualize-sidebar',

    data() {
        return {
            selectedChart: this.$t('noVisualization'),
            chartTypes: [
                this.$t('noVisualization'),
                'Line',
                'Scatter',
                'Bar - Vertical',
                'Bar - Horizontal',
            ],
            resSet: null,
            axis: {
                x: '',
                y: '',
            },
            numberSign: '#',
            showTrendline: false,
            rmResultSetsWatcher: null,
        }
    },
    computed: {
        ...mapState({
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
        }),
        ...mapGetters({
            getPrvwDataRes: 'query/getPrvwDataRes',
            getResults: 'query/getResults',
        }),
        resultSets() {
            let resSets = []

            let resSetArr = this.cloneRes(
                this.$typy(this.getResults, 'attributes.results').safeArray
            )
            let resSetCount = 0
            for (const res of resSetArr) {
                if (res.data) {
                    ++resSetCount
                    res.id = `RESULT SET ${resSetCount}`
                    resSets.push(res)
                }
            }

            let prvwData = this.cloneRes(
                this.$typy(this.getPrvwDataRes(this.SQL_QUERY_MODES.PRVW_DATA)).safeObject
            )
            if (!this.$typy(prvwData).isEmptyObject) {
                prvwData.id = this.$t('previewData')
                resSets.push(prvwData)
            }

            let prvwDataDetails = this.cloneRes(
                this.$typy(this.getPrvwDataRes(this.SQL_QUERY_MODES.PRVW_DATA_DETAILS)).safeObject
            )

            if (!this.$typy(prvwDataDetails).isEmptyObject) {
                prvwDataDetails.id = this.$t('viewDetails')
                resSets.push(prvwDataDetails)
            }
            return resSets
        },

        numericFields() {
            // Iterates the first row to get column type
            let types = []
            this.resSet.data[0].forEach(col => {
                types.push(typeof col)
            })
            // get numeric column indexes
            let indices = []
            const numType = 'number'
            let idx = types.indexOf(numType)
            while (idx != -1) {
                indices.push(idx)
                idx = types.indexOf(numType, idx + 1)
            }
            // show numeric fields only
            let fields = [
                this.numberSign,
                ...this.resSet.fields.filter((_, i) => indices.includes(i)),
            ]
            return fields
        },
        xAxisFields() {
            if (this.$typy(this.resSet, 'fields').isEmptyArray) return []
            switch (this.selectedChart) {
                case 'Bar - Horizontal':
                    return this.numericFields
                // linear, category or time cartesian axes
                case 'Scatter':
                case 'Line':
                case 'Bar - Vertical':
                default:
                    return [this.numberSign, ...this.resSet.fields]
            }
        },
        yAxisFields() {
            if (this.$typy(this.resSet, 'fields').isEmptyArray) return []
            switch (this.selectedChart) {
                case 'Line':
                case 'Scatter':
                case 'Bar - Vertical':
                    return this.numericFields
                // linear, category or time cartesian axes
                case 'Bar - Horizontal':
                default:
                    return [this.numberSign, ...this.resSet.fields]
            }
        },
        supportTrendLine() {
            return (
                this.selectedChart === 'Line' ||
                this.selectedChart === 'Scatter' ||
                this.selectedChart.includes('Bar')
            )
        },
    },
    watch: {
        selectedChart(v) {
            this.$emit('selected-chart', v)
            this.clearAxisVal()
        },
        resSet: {
            deep: true,
            handler() {
                this.clearAxisVal()
            },
        },
        axis: {
            deep: true,
            handler(v) {
                this.genChartData({ axis: v, chartType: this.selectedChart })
            },
        },
        showTrendline() {
            this.genChartData({ axis: this.axis, chartType: this.selectedChart })
        },
    },
    deactivated() {
        this.rmResultSetsWatcher()
    },
    activated() {
        this.addResultSetsWatcher()
    },
    methods: {
        addResultSetsWatcher() {
            // store watcher to rmResultSetsWatcher and use it for removing the watcher
            this.rmResultSetsWatcher = this.$watch('resultSets', (v, oV) => {
                if (!this.$help.lodash.isEqual(v, oV)) {
                    this.clearAxisVal()
                    this.resSet = null
                    this.genChartData({ axis: this.axis, chartType: this.selectedChart })
                }
            })
        },
        cloneRes(res) {
            return JSON.parse(JSON.stringify(res))
        },
        clearAxisVal() {
            this.axis = { x: '', y: '' }
        },
        genDataset({ colorIndex, data, chartType }) {
            const lineColor = this.$help.dynamicColors(colorIndex)
            let dataset = {
                data,
            }
            const indexOfOpacity = lineColor.lastIndexOf(')') - 1
            const backgroundColor = this.$help.strReplaceAt({
                str: lineColor,
                index: indexOfOpacity,
                newChar: '0.2',
            })
            switch (chartType) {
                case 'Line':
                    {
                        dataset = {
                            ...dataset,
                            backgroundColor: backgroundColor,
                            borderColor: lineColor,
                            lineTension: 0,
                            borderWidth: 1,
                            pointBorderColor: 'transparent',
                            pointBackgroundColor: 'transparent',
                            pointHoverBorderColor: lineColor,
                            pointHoverBackgroundColor: backgroundColor,
                        }
                    }
                    break
                case 'Scatter': {
                    dataset = {
                        ...dataset,
                        borderWidth: 1,
                        backgroundColor: backgroundColor,
                        borderColor: lineColor,
                        pointHoverBackgroundColor: lineColor,
                        pointHoverRadius: 5,
                    }
                    break
                }
                case 'Bar - Vertical':
                case 'Bar - Horizontal': {
                    dataset = {
                        ...dataset,
                        barPercentage: 0.5,
                        categoryPercentage: 1,
                        barThickness: 'flex',
                        maxBarThickness: 48,
                        minBarLength: 2,
                        backgroundColor: backgroundColor,
                        borderColor: lineColor,
                        borderWidth: 1,
                        hoverBackgroundColor: lineColor,
                        hoverBorderColor: '#4f5051',
                    }
                    break
                }
            }

            if (this.showTrendline && this.supportTrendLine)
                dataset.trendlineLinear = {
                    style: '#2d9cdb',
                    lineStyle: 'solid',
                    width: 2,
                }
            return dataset
        },

        isLinearAxes(axisVal) {
            return typeof axisVal === 'number'
        },
        isTimeAxes(axisVal) {
            return this.$moment(axisVal).isValid()
        },

        /** This mutates sorting chart data for linear axes
         * @param {Object} chartData - ChartData object
         * @param {String} labelAxisId - axis id: x or y
         * @param {Boolean} isDate - if data is date string
         */
        sortingChartData({ chartData, labelAxisId, isDate = false }) {
            chartData.labels.sort((a, b) => (isDate ? this.$moment(a) - this.$moment(b) : a - b))
            chartData.datasets[0].data.sort((a, b) =>
                isDate
                    ? this.$moment(a[labelAxisId]) - this.$moment(b[labelAxisId])
                    : a[labelAxisId] - b[labelAxisId]
            )
        },

        genChartData({ axis, chartType }) {
            let xAxisType = 'category'
            let labelAxisId = 'x'
            let axisLabels = { x: '', y: '' }
            let chartData = {
                labels: [],
                datasets: [],
            }
            if (axis.x && axis.y) {
                axisLabels = { x: axis.x, y: axis.y }
                let dataPoints = []
                let labels = []
                const dataRows = this.$help.getObjectRows({
                    columns: this.resSet.fields,
                    rows: this.resSet.data,
                })

                for (const [i, row] of dataRows.entries()) {
                    const rowNumber = i + 1
                    const isXAxisARowNum = axis.x === this.numberSign
                    const isYAxisARowNum = axis.y === this.numberSign
                    const xAxisVal = isXAxisARowNum ? rowNumber : row[axis.x]
                    const yAxisVal = isYAxisARowNum ? rowNumber : row[axis.y]

                    dataPoints.push({
                        dataPointObj: row,
                        x: xAxisVal,
                        y: yAxisVal,
                        xLabel: axis.x,
                        yLabel: axis.y,
                    })

                    switch (chartType) {
                        case 'Bar - Horizontal':
                            labels.push(yAxisVal)
                            break
                        default:
                            labels.push(xAxisVal)
                    }
                }

                const dataset = this.genDataset({ colorIndex: 0, data: dataPoints, chartType })

                chartData = {
                    labels,
                    datasets: [dataset],
                }

                switch (chartType) {
                    case 'Bar - Horizontal':
                        labelAxisId = 'y'
                        break
                    default:
                        labelAxisId = 'x'
                }

                if (this.isLinearAxes(dataset.data[0][labelAxisId])) xAxisType = 'linear'
                else if (this.isTimeAxes(dataset.data[0][labelAxisId])) xAxisType = 'time'

                switch (xAxisType) {
                    case 'linear':
                        this.sortingChartData({ chartData, labelAxisId })
                        break
                    case 'time':
                        this.sortingChartData({ chartData, labelAxisId, isDate: true })
                        break
                }
            }

            this.$emit('get-chart-data', chartData)
            this.$emit('get-axis-labels', axisLabels)
            this.$emit('x-axis-type', xAxisType)
        },
    },
}
</script>

<style lang="scss" scoped>
::v-deep .show-trendline label {
    font-size: $label-control-size;
    color: $small-text;
    font-weight: 400;
}
</style>
