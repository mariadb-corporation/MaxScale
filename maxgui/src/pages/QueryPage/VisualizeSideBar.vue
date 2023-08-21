<template>
    <div class="pa-4">
        <h5 class="mb-4">{{ $t('visualization') }}</h5>
        <label class="field__label color text-small-text label-required"> {{ $t('graph') }}</label>
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
            <label class="field__label color text-small-text label-required">
                {{ $t('selectResultSet') }}
            </label>
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
                <!-- Don't show scaleLabels inputs if result set is empty -->
                <div v-if="$typy(resSet, 'data').isEmptyArray" class="mt-4 color text-small-text">
                    {{ $t('emptySet') }}
                </div>
                <template v-else>
                    <div v-for="(_, axisId) in scaleLabels" :key="axisId">
                        <div class="mt-2">
                            <label
                                class="field__label color text-small-text text-capitalize label-required"
                            >
                                {{ axisId }} axis
                            </label>
                            <v-select
                                v-model="scaleLabels[axisId]"
                                :items="axisFields"
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
                        <div class="mt-2">
                            <label
                                class="field__label color text-small-text text-capitalize label-required"
                            >
                                {{ axisId }} axis type
                            </label>
                            <v-select
                                v-model="axesType[axisId]"
                                :items="Object.values(SQL_CHART_AXIS_TYPES)"
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
                    class="mt-2 show-trendline"
                    hide-details
                >
                    <template v-slot:label>
                        <label class="v-label">{{ $t('showTrendline') }}</label>
                        <v-tooltip
                            top
                            transition="slide-y-transition"
                            content-class="shadow-drop color text-navigation py-1 px-4"
                        >
                            <template v-slot:activator="{ on }">
                                <v-icon
                                    class="ml-1 material-icons-outlined pointer"
                                    size="16"
                                    color="#9DB4BB"
                                    v-on="on"
                                >
                                    info
                                </v-icon>
                            </template>
                            <span>{{ $t('info.showTrendline') }}</span>
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
 * Change Date: 2027-08-18
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
            scaleLabels: { x: '', y: '' }, // scaleLabels inputs
            axesType: { x: '', y: '' }, // axesType inputs
            showTrendline: false,
            rmResultSetsWatcher: null,
        }
    },
    computed: {
        ...mapState({
            SQL_QUERY_MODES: state => state.app_config.SQL_QUERY_MODES,
            SQL_CHART_TYPES: state => state.app_config.SQL_CHART_TYPES,
            SQL_CHART_AXIS_TYPES: state => state.app_config.SQL_CHART_AXIS_TYPES,
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
        axisFields() {
            if (this.$typy(this.resSet, 'fields').isEmptyArray) return []
            return this.resSet.fields
        },
        supportTrendLine() {
            const { LINE, SCATTER, BAR_VERT, BAR_HORIZ } = this.SQL_CHART_TYPES
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
                    this.clearAxes()
                    this.resSet = null
                    this.genChartData()
                }
            })
        },
        cloneRes(res) {
            return JSON.parse(JSON.stringify(res))
        },
        clearAxes() {
            this.scaleLabels = { x: '', y: '' }
            this.axesType = { x: '', y: '' }
        },
        labelingAxisType(axisType) {
            const { LINEAR, CATEGORY } = this.SQL_CHART_AXIS_TYPES
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

        /** This mutates sorting chart data for LINEAR or TIME axis
         * @param {Object} data - ChartData object
         * @param {String} axisId - axis id: x or y
         * @param {String} axisType - LINEAR or TIME axis
         */
        sortingChartData({ data, axisId, axisType }) {
            const { LINEAR, TIME } = this.SQL_CHART_AXIS_TYPES
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
                const dataRows = this.$help.getObjectRows({
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
                const { BAR_HORIZ } = this.SQL_CHART_TYPES
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

<style lang="scss" scoped>
::v-deep .show-trendline label {
    font-size: $label-control-size;
    color: $small-text;
    font-weight: 400;
}
</style>
