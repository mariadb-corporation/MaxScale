<template>
    <div class="pa-4">
        <h5 class="mb-4">{{ $t('visualization') }}</h5>
        <label class="field__label color text-small-text"> {{ $t('graph') }}</label>
        <v-select
            v-model="chartOpt.type"
            :items="Object.values(SQL_CHART_TYPES)"
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
        <div v-if="chartOpt.type" class="mt-4">
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
                <template v-else>
                    <div v-for="(_, axisName) in axis" :key="axisName" class="mt-2">
                        <label class="field__label color text-small-text text-capitalize">
                            {{ axisName }} axis
                        </label>
                        <v-select
                            v-model="axis[axisName]"
                            :items="axisName === 'y' ? yAxisFields : xAxisFields"
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
 * Change Date: 2026-04-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { mapGetters, mapState } from 'vuex'
export default {
    name: 'visualize-sidebar',
    props: { value: { type: Object, required: true } },
    data() {
        return {
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
            SQL_CHART_TYPES: state => state.app_config.SQL_CHART_TYPES,
        }),
        ...mapGetters({
            getPrvwDataRes: 'query/getPrvwDataRes',
            getResults: 'query/getResults',
            getActiveTreeNode: 'query/getActiveTreeNode',
        }),
        chartOpt: {
            get() {
                return this.value
            },
            set(value) {
                this.$emit('input', value)
            },
        },
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
                prvwData.id = `${this.$t('previewData')} for ${this.getActiveTreeNode.id}`
                resSets.push(prvwData)
            }

            let prvwDataDetails = this.cloneRes(
                this.$typy(this.getPrvwDataRes(this.SQL_QUERY_MODES.PRVW_DATA_DETAILS)).safeObject
            )

            if (!this.$typy(prvwDataDetails).isEmptyObject) {
                prvwDataDetails.id = `${this.$t('viewDetails')} for ${this.getActiveTreeNode.id}`
                resSets.push(prvwDataDetails)
            }
            return resSets
        },
        numericFields() {
            let indices = []
            this.resSet.data[0].forEach((cell, i) => {
                if (this.IsNumericCell(cell)) indices.push(i)
            })
            // show numeric fields only
            let fields = [
                this.numberSign,
                ...this.resSet.fields.filter((_, i) => indices.includes(i)),
            ]
            return fields
        },
        xAxisFields() {
            if (this.$typy(this.resSet, 'fields').isEmptyArray) return []
            const { LINE, SCATTER, BAR_VERT, BAR_HORIZ } = this.SQL_CHART_TYPES
            switch (this.chartOpt.type) {
                case BAR_HORIZ:
                    return this.numericFields
                // linear, category or time cartesian axes
                case SCATTER:
                case LINE:
                case BAR_VERT:
                default:
                    return [this.numberSign, ...this.resSet.fields]
            }
        },
        yAxisFields() {
            if (this.$typy(this.resSet, 'fields').isEmptyArray) return []
            const { LINE, SCATTER, BAR_VERT, BAR_HORIZ } = this.SQL_CHART_TYPES
            switch (this.chartOpt.type) {
                case LINE:
                case SCATTER:
                case BAR_VERT:
                    return this.numericFields
                // linear, category or time cartesian axes
                case BAR_HORIZ:
                default:
                    return [this.numberSign, ...this.resSet.fields]
            }
        },
        supportTrendLine() {
            const { LINE, SCATTER, BAR_VERT, BAR_HORIZ } = this.SQL_CHART_TYPES
            return [LINE, SCATTER, BAR_VERT, BAR_HORIZ].includes(this.chartOpt.type)
        },
    },
    watch: {
        'chartOpt.type'() {
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
            handler() {
                this.genChartData()
            },
        },
        showTrendline() {
            this.genChartData()
        },
    },
    deactivated() {
        if (this.rmResultSetsWatcher) this.rmResultSetsWatcher()
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
                    this.genChartData()
                }
            })
        },
        cloneRes(res) {
            return JSON.parse(JSON.stringify(res))
        },
        clearAxisVal() {
            this.axis = { x: '', y: '' }
        },
        genDataset({ colorIndex, data }) {
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
            const { LINE, SCATTER, BAR_VERT, BAR_HORIZ } = this.SQL_CHART_TYPES
            switch (this.chartOpt.type) {
                case LINE:
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
                case SCATTER: {
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
                case BAR_VERT:
                case BAR_HORIZ: {
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
        // parse value to float number and check if it is number
        IsNumericCell(cell) {
            return this.$typy(parseFloat(cell)).isNumber
        },
        isLinearAxes(axisVal) {
            return this.IsNumericCell(axisVal)
        },
        isTimeAxes(axisVal) {
            return this.$moment(axisVal).isValid()
        },

        /** This mutates sorting chart data for linear axes
         * @param {Object} data - ChartData object
         * @param {String} labelAxisId - axis id: x or y
         * @param {Boolean} isDate - if data is date string
         */
        sortingChartData({ data, labelAxisId, isDate = false }) {
            data.labels.sort((a, b) => (isDate ? this.$moment(a) - this.$moment(b) : a - b))
            data.datasets[0].data.sort((a, b) =>
                isDate
                    ? this.$moment(a[labelAxisId]) - this.$moment(b[labelAxisId])
                    : a[labelAxisId] - b[labelAxisId]
            )
        },

        genChartData() {
            const { x, y } = this.axis
            let xAxisType = 'category',
                labelAxisId = 'x',
                axisLabels = { x: '', y: '' },
                data = {
                    labels: [],
                    datasets: [],
                }
            if (x && y) {
                axisLabels = { x, y }
                let dataPoints = []
                let labels = []
                const dataRows = this.$help.getObjectRows({
                    columns: this.resSet.fields,
                    rows: this.resSet.data,
                })
                const { BAR_HORIZ } = this.SQL_CHART_TYPES
                for (const [i, row] of dataRows.entries()) {
                    const rowNumber = i + 1
                    const isXAxisARowNum = x === this.numberSign
                    const isYAxisARowNum = y === this.numberSign
                    const xAxisVal = isXAxisARowNum ? rowNumber : row[x]
                    const yAxisVal = isYAxisARowNum ? rowNumber : row[y]

                    dataPoints.push({
                        dataPointObj: row,
                        x: xAxisVal,
                        y: yAxisVal,
                        xLabel: x,
                        yLabel: y,
                    })

                    switch (this.chartOpt.type) {
                        case BAR_HORIZ:
                            labels.push(yAxisVal)
                            break
                        default:
                            labels.push(xAxisVal)
                    }
                }

                const dataset = this.genDataset({ colorIndex: 0, data: dataPoints })

                data = {
                    labels,
                    datasets: [dataset],
                }

                switch (this.chartOpt.type) {
                    case BAR_HORIZ:
                        labelAxisId = 'y'
                        break
                    default:
                        labelAxisId = 'x'
                }

                if (this.isLinearAxes(dataset.data[0][labelAxisId])) xAxisType = 'linear'
                else if (this.isTimeAxes(dataset.data[0][labelAxisId])) xAxisType = 'time'

                switch (xAxisType) {
                    case 'linear':
                        this.sortingChartData({ data, labelAxisId })
                        break
                    case 'time':
                        this.sortingChartData({ data, labelAxisId, isDate: true })
                        break
                }
            }
            this.chartOpt = { ...this.chartOpt, data, axisLabels, xAxisType }
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
