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
                v-model="resSetId"
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
            />
            <template v-if="resSetId">
                <template v-for="a in ['x', 'y']">
                    <div :key="a" class="mt-2">
                        <label class="field__label color text-small-text text-capitalize">
                            {{ a }} axis
                        </label>
                        <!-- TODO: Show only numeric value field in y axis -->
                        <v-select
                            v-model="axis[a]"
                            :items="resultSetMap.get(resSetId).fields"
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
            chartTypes: ['No Visualization', 'Line', 'Bar - Horizontal', 'Bar - Vertical'],
            resSetId: null,
            axis: {
                x: '',
                y: '',
            },
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
            /* TODO: Add row index to each resSets */
            return resSets
        },
        resultSetMap() {
            let map = new Map()
            this.resultSets.forEach(ele => map.set(ele.id, ele))
            return map
        },
    },
    watch: {
        selectedChart(v) {
            this.$emit('selected-chart', v)
        },
        resultSets: {
            deep: true,
            handler() {
                /** TODO: When resultSetMap changes its size, genChartData will be
                 *  failed if chosen resSetId is not in the map.
                 *  Possible workaround is to clear resSetId
                 */
                this.genChartData(this.axis)
            },
        },
        resSetId() {
            // Clear axis value
            this.axis = { x: '', y: '' }
        },
        axis: {
            deep: true,
            handler(v) {
                this.genChartData(v)
            },
        },
    },
    methods: {
        genDataset({ colorIndex, data }) {
            const lineColor = this.$help.dynamicColors(colorIndex)
            const indexOfOpacity = lineColor.lastIndexOf(')') - 1
            const backgroundColor = this.$help.strReplaceAt({
                str: lineColor,
                index: indexOfOpacity,
                newChar: '0.1',
            })
            let dataset = {
                type: 'line',
                // background of the line
                backgroundColor: backgroundColor,
                borderColor: lineColor,
                borderWidth: 1,
                lineTension: 0,
                data,
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
        genChartData(axis) {
            if (axis.x && axis.y) {
                let data = []
                let xLabels = []
                const dataRows = this.getObjectRows({
                    columns: this.resultSetMap.get(this.resSetId).fields,
                    rows: this.resultSetMap.get(this.resSetId).data,
                })
                for (const row of dataRows) {
                    data.push({ ...row, x: row[axis.x], y: row[axis.y] })
                    xLabels.push(row[axis.x])
                }
                const dataset = this.genDataset({ colorIndex: 0, data })
                const chartData = {
                    labels: xLabels,
                    datasets: [dataset],
                }
                this.$emit('get-axis-labels', { x: axis.x, y: axis.y })
                this.$emit('get-chart-data', chartData)
            }
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
