<template>
    <div class="pa-4">
        <h5 class="mb-4">Visualization</h5>
        <label class="field__label color text-small-text"> Graph</label>
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
        <div v-if="selectedChart !== 'No Visualization'" class="mt-4">
            <label class="field__label color text-small-text"> Select result set</label>
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
                    Empty set
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
 * Change Date: 2025-05-25
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
            selectedChart: 'No Visualization',
            chartTypes: [
                'No Visualization',
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
        }
    },
    computed: {
        ...mapState({
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
            query_result: state => state.query.query_result,
        }),
        ...mapGetters({
            getPrvwDataRes: 'query/getPrvwDataRes',
        }),

        resultSets() {
            let resSets = []

            let resSetArr = this.$typy(this.query_result, 'attributes.results').safeArray
            for (const [i, res] of resSetArr.entries()) {
                if (res.data) {
                    res.id = `RESULT SET ${i + 1}`
                    resSets.push(res)
                }
            }

            let prvwData = this.$typy(this.getPrvwDataRes(this.SQL_QUERY_MODES.PRVW_DATA))
                .safeObject
            if (!this.$typy(prvwData).isEmptyObject) {
                prvwData.id = this.$t('previewData')
                resSets.push(prvwData)
            }

            let prvwDataDetails = this.$typy(
                this.getPrvwDataRes(this.SQL_QUERY_MODES.PRVW_DATA_DETAILS)
            ).safeObject

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
                case 'Scatter':
                case 'Bar - Horizontal':
                    return this.numericFields
                // Either linear or category cartesian axes
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
                // Either linear or category cartesian axes
                case 'Bar - Horizontal':
                default:
                    return [this.numberSign, ...this.resSet.fields]
            }
        },
    },
    watch: {
        selectedChart(v) {
            this.$emit('selected-chart', v)
            this.clearAxisVal()
        },
        resultSets: {
            deep: true,
            handler() {
                this.clearAxisVal()
                this.resSet = null
                this.genChartData({ axis: this.axis, chartType: this.selectedChart })
            },
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
    },
    methods: {
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
                        backgroundColor: lineColor,
                        borderColor: lineColor,
                        borderWidth: 1,
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
            return dataset
        },
        getObjectRows({ columns, rows }) {
            return rows.map(row => {
                const obj = {}
                columns.forEach((c, index) => {
                    obj[c] = row[index]
                })
                return obj
            })
        },
        isLinearAxes(axisVal) {
            return typeof axisVal === 'number'
        },

        /** This mutates sorting chart data for linear axes
         * @param {Object} chartData - ChartData object
         * @param {String} linearAxisId - axis id: x or y
         */
        sortingLinearData(chartData, linearAxisId) {
            chartData.labels.sort((a, b) => a - b)
            chartData.datasets[0].data.sort((a, b) => {
                if (a[linearAxisId] < b[linearAxisId]) return -1
                if (a[linearAxisId] > b[linearAxisId]) return 1
                return 0
            })
        },

        genChartData({ axis, chartType }) {
            let isLinear = false
            let linearAxisId = 'x'
            let axisLabels = { x: '', y: '' }
            let chartData = {
                labels: [],
                datasets: [],
            }
            if (axis.x && axis.y) {
                axisLabels = { x: axis.x, y: axis.y }
                let dataPoints = []
                let labels = []
                const dataRows = this.getObjectRows({
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
                        linearAxisId = 'y'
                        break
                    default:
                        linearAxisId = 'x'
                }
                isLinear = this.isLinearAxes(dataset.data[0][linearAxisId])
                if (isLinear) this.sortingLinearData(chartData, linearAxisId)
            }

            this.$emit('get-chart-data', chartData)
            this.$emit('get-axis-labels', axisLabels)
            this.$emit('is-linear-chart', isLinear)
        },
    },
}
</script>

<style lang="scss" scoped>
$label-size: 0.75rem;
.field__label {
    font-size: $label-size;
}
</style>
