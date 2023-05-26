<template>
    <div class="pa-4">
        <h5 class="mb-4">{{ $mxs_t('visualization') }}</h5>
        <label class="field__label mxs-color-helper text-small-text label-required">
            {{ $mxs_t('graph') }}</label
        >
        <v-select
            v-model="chartOpt.type"
            :items="Object.values(chartTypes)"
            outlined
            class="vuetify-input--override v-select--mariadb error--text__bottom"
            :menu-props="{
                contentClass: 'v-select--menu-mariadb',
                bottom: true,
                offsetY: true,
            }"
            dense
            :height="36"
            hide-details="auto"
        />
        <div v-if="chartOpt.type" class="mt-4">
            <label class="field__label mxs-color-helper text-small-text label-required">
                {{ $mxs_t('selectResultSet') }}
            </label>
            <v-select
                v-model="resSet"
                :items="resultSets"
                outlined
                class="vuetify-input--override v-select--mariadb error--text__bottom"
                :menu-props="{
                    contentClass: 'v-select--menu-mariadb',
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
                <!-- Don't show scaleLabels inputs if result set is empty -->
                <div
                    v-if="$typy(resSet, 'data').isEmptyArray"
                    class="mt-4 mxs-color-helper text-small-text"
                >
                    {{ $mxs_t('emptySet') }}
                </div>
                <template v-else>
                    <div v-for="(_, axisId) in scaleLabels" :key="axisId">
                        <div class="mt-2">
                            <label
                                class="field__label mxs-color-helper text-small-text text-capitalize label-required"
                            >
                                {{ axisId }} axis
                            </label>
                            <v-select
                                v-model="scaleLabels[axisId]"
                                :items="axisFields"
                                outlined
                                class="vuetify-input--override v-select--mariadb error--text__bottom"
                                :menu-props="{
                                    contentClass: 'v-select--menu-mariadb',
                                    bottom: true,
                                    offsetY: true,
                                }"
                                dense
                                :height="36"
                                hide-details="auto"
                            />
                        </div>
                        <div class="mt-2">
                            <label
                                class="field__label mxs-color-helper text-small-text text-capitalize label-required"
                            >
                                {{ axisId }} axis type
                            </label>
                            <v-select
                                v-model="axesType[axisId]"
                                :items="Object.values(axisTypes)"
                                outlined
                                class="vuetify-input--override v-select--mariadb error--text__bottom"
                                :menu-props="{
                                    contentClass: 'v-select--menu-mariadb',
                                    bottom: true,
                                    offsetY: true,
                                }"
                                dense
                                :height="36"
                                hide-details="auto"
                            >
                                <template v-slot:selection="{ item }">
                                    {{ labelingAxisType(item) }}
                                </template>
                                <template v-slot:item="{ item }">
                                    {{ labelingAxisType(item) }}
                                </template>
                            </v-select>
                        </div>
                    </div>
                </template>
                <v-checkbox
                    v-if="supportTrendLine"
                    v-model="showTrendline"
                    dense
                    color="primary"
                    class="mt-2 v-checkbox--mariadb"
                    hide-details
                >
                    <template v-slot:label>
                        <label class="v-label">{{ $mxs_t('showTrendline') }}</label>
                        <v-tooltip top transition="slide-y-transition">
                            <template v-slot:activator="{ on }">
                                <v-icon
                                    class="ml-1 material-icons-outlined pointer"
                                    size="16"
                                    color="info"
                                    v-on="on"
                                >
                                    mdi-information-outline
                                </v-icon>
                            </template>
                            <span>{{ $mxs_t('info.showTrendline') }}</span>
                        </v-tooltip>
                    </template>
                </v-checkbox>
            </template>
            <!-- TODO: add more graph configurations -->
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
 * Change Date: 2027-05-22
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
export default {
    name: 'chart-config',
    props: {
        value: { type: Object, required: true },
        chartTypes: { type: Object, required: true }, // SQL_CHART_TYPES object
        axisTypes: { type: Object, required: true }, // CHART_AXIS_TYPES object
        queryModes: { type: Object, required: true }, // QUERY_MODES object
        resultSets: { type: Array, required: true },
    },
    data() {
        return {
            resSet: null,
            scaleLabels: { x: '', y: '' }, // scaleLabels inputs
            axesType: { x: '', y: '' }, // axesType inputs
            showTrendline: false,
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
        axisFields() {
            if (this.$typy(this.resSet, 'fields').isEmptyArray) return []
            return this.resSet.fields
        },
        supportTrendLine() {
            const { LINE, SCATTER, BAR_VERT, BAR_HORIZ } = this.chartTypes
            return [LINE, SCATTER, BAR_VERT, BAR_HORIZ].includes(this.chartOpt.type)
        },
    },
    watch: {
        'chartOpt.type'() {
            this.clearAxes()
        },
        resSet: {
            deep: true,
            handler() {
                this.clearAxes()
            },
        },
        scaleLabels: {
            deep: true,
            handler() {
                this.genChartData()
            },
        },
        axesType: {
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
        this.$typy(this.unwatch_resultSets).safeFunction()
    },
    activated() {
        this.watch_resultSets()
    },
    methods: {
        watch_resultSets() {
            // store watcher to unwatch_resultSets and use it for removing the watcher
            this.unwatch_resultSets = this.$watch('resultSets', (v, oV) => {
                if (!this.$helpers.lodash.isEqual(v, oV)) {
                    this.clearAxes()
                    this.resSet = null
                    this.genChartData()
                }
            })
        },
        clearAxes() {
            this.scaleLabels = { x: '', y: '' }
            this.axesType = { x: '', y: '' }
        },
        labelingAxisType(axisType) {
            const { LINEAR, CATEGORY } = this.axisTypes
            switch (axisType) {
                case LINEAR:
                    return `${axisType} (Numerical data)`
                case CATEGORY:
                    return `${axisType} (String data)`
                default:
                    return axisType
            }
        },
        genDataset({ colorIndex, data }) {
            const lineColor = this.$helpers.dynamicColors(colorIndex)
            let dataset = {
                data,
            }
            const indexOfOpacity = lineColor.lastIndexOf(')') - 1
            const backgroundColor = this.$helpers.strReplaceAt({
                str: lineColor,
                index: indexOfOpacity,
                newChar: '0.2',
            })
            const { LINE, SCATTER, BAR_VERT, BAR_HORIZ } = this.chartTypes
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

        /** This mutates sorting chart data for LINEAR or TIME axis
         * @param {Object} data - ChartData object
         * @param {String} axisId - axis id: x or y
         * @param {String} axisType - LINEAR or TIME axis
         */
        sortingChartData({ data, axisId, axisType }) {
            const { LINEAR, TIME } = this.axisTypes
            switch (axisType) {
                case LINEAR:
                    data.xLabels.sort((a, b) => a - b)
                    data.datasets[0].data.sort((a, b) => a[axisId] - b[axisId])
                    break
                case TIME:
                    data.xLabels.sort((a, b) => this.$moment(a) - this.$moment(b))
                    data.datasets[0].data.sort(
                        (a, b) => this.$moment(a[axisId]) - this.$moment(b[axisId])
                    )
                    break
            }
        },

        genChartData() {
            let axesType = this.axesType,
                scaleLabels = this.scaleLabels,
                data = {
                    xLabels: [],
                    yLabels: [],
                    datasets: [],
                }
            if (scaleLabels.x && scaleLabels.y && axesType.x && axesType.y) {
                let dataPoints = []
                const dataRows = this.$helpers.getObjectRows({
                    columns: this.resSet.fields,
                    rows: this.resSet.data,
                })

                for (const row of dataRows) {
                    const xAxisVal = row[scaleLabels.x]
                    const yAxisVal = row[scaleLabels.y]

                    dataPoints.push({
                        dataPointObj: row,
                        x: xAxisVal,
                        y: yAxisVal,
                        scaleLabelX: scaleLabels.x,
                        scaleLabelY: scaleLabels.y,
                    })
                    data.xLabels.push(xAxisVal)
                    data.yLabels.push(yAxisVal)
                }

                const dataset = this.genDataset({ colorIndex: 0, data: dataPoints })
                data.datasets = [dataset]
                // handle sorting chart data for LINEAR or TIME axis
                const { BAR_HORIZ } = this.chartTypes
                let axisIdToBeSorted = 'x'
                // For vertical graphs, sort only the X axis, but for horizontal, sort the Y axis
                switch (this.chartOpt.type) {
                    case BAR_HORIZ:
                        axisIdToBeSorted = 'y'
                        break
                }
                this.sortingChartData({
                    data,
                    axisId: axisIdToBeSorted,
                    axisType: axesType[axisIdToBeSorted],
                })
            }
            this.chartOpt = { ...this.chartOpt, data, scaleLabels, axesType }
        },
    },
}
</script>
