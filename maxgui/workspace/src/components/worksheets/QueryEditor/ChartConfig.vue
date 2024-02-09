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
                <!-- Don't show axisKeys inputs if result set is empty -->
                <div
                    v-if="$typy(resSet, 'data').isEmptyArray"
                    class="mt-4 mxs-color-helper text-small-text"
                >
                    {{ $mxs_t('emptySet') }}
                </div>
                <template v-else>
                    <div v-for="(_, axisId) in axisKeys" :key="axisId">
                        <div class="mt-2">
                            <label
                                class="field__label mxs-color-helper text-small-text text-capitalize label-required"
                            >
                                {{ axisId }} axis
                            </label>
                            <v-select
                                v-model="axisKeys[axisId]"
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
 * Change Date: 2028-01-30
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
            axisKeys: { x: '', y: '' }, // axisKeys inputs
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
        isHorizChart() {
            return this.chartOpt.type === this.chartTypes.BAR_HORIZ
        },
        supportTrendLine() {
            const { LINE, SCATTER, BAR_VERT, BAR_HORIZ } = this.chartTypes
            return [LINE, SCATTER, BAR_VERT, BAR_HORIZ].includes(this.chartOpt.type)
        },
        hasLinearAxis() {
            const { LINEAR } = this.axisTypes
            return this.axesType.x === LINEAR || this.axesType.y === LINEAR
        },
    },
    watch: {
        resultSets: {
            deep: true,
            handler(v, oV) {
                if (!this.$helpers.lodash.isEqual(v, oV)) {
                    this.clearAxes()
                    this.resSet = null
                    this.genChartData()
                }
            },
        },
        'chartOpt.type'() {
            this.clearAxes()
        },
        resSet: {
            deep: true,
            handler() {
                this.clearAxes()
            },
        },
        axisKeys: {
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
    methods: {
        clearAxes() {
            this.axisKeys = { x: '', y: '' }
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
        genDatasetProperties() {
            const lineColor = this.$helpers.dynamicColors(0)
            let dataset = {}
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
                            fill: true,
                            backgroundColor: backgroundColor,
                            borderColor: lineColor,
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
                        borderWidth: 1,
                        fill: true,
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
        /** This mutates sorting chart data for LINEAR or TIME axis
         * @param {Array} tableData - Table data
         */
        sortLinearOrTimeData(tableData) {
            const { BAR_HORIZ } = this.chartTypes
            let axisId = 'y'
            // For vertical graphs, sort only the y axis, but for horizontal, sort the x axis
            if (this.chartOpt.type === BAR_HORIZ) axisId = 'x'
            const axisType = this.axesType[axisId]
            const { LINEAR, TIME } = this.axisTypes
            const axisKeys = this.axisKeys
            if (axisType === LINEAR || axisType === TIME) {
                tableData.sort((a, b) => {
                    const valueA = a[axisKeys[axisId]]
                    const valueB = b[axisKeys[axisId]]
                    if (axisType === this.axisTypes.LINEAR) return valueA - valueB
                    return new Date(valueA) - new Date(valueB)
                })
            }
        },
        genChartData() {
            const { axisKeys, isHorizChart, axesType, resSet } = this
            const { x, y } = axisKeys
            let chartData = {
                datasets: [{ data: [], ...this.genDatasetProperties() }],
                labels: [],
            }
            const tableData = this.$helpers.map2dArr({
                fields: this.$typy(resSet, 'fields').safeArray,
                arr: this.$typy(resSet, 'data').safeArray,
            })
            if (x && y && axesType.x && axesType.y) {
                this.sortLinearOrTimeData(tableData)
                tableData.forEach(row => {
                    const dataPoint = isHorizChart ? row[x] : row[y]
                    const label = isHorizChart ? row[y] : row[x]
                    chartData.datasets[0].data.push(dataPoint)
                    chartData.labels.push(label)
                })
            }
            this.chartOpt = {
                ...this.chartOpt,
                chartData,
                axisKeys,
                axesType,
                tableData,
                isHorizChart,
                hasTrendline: this.hasLinearAxis && this.showTrendline && this.supportTrendLine,
            }
        },
    },
}
</script>
