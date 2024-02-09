<template>
    <collapsible-ctr :title="$mxs_t('resTimeDist')">
        <template v-slot:title-append>
            <v-menu
                open-on-hover
                top
                offset-y
                transition="slide-y-transition"
                max-width="400"
                content-class="shadow-drop mxs-color-helper text-navigation rounded-sm"
                :close-delay="300"
            >
                <template v-slot:activator="{ on }">
                    <v-icon
                        class="ml-1 material-icons-outlined pointer"
                        size="18"
                        color="info"
                        v-on="on"
                    >
                        mdi-information-outline
                    </v-icon>
                </template>
                <i18n
                    path="mxs.info.resTimeDist"
                    tag="div"
                    class="res-time-dist-histogram-info-tooltip-menu py-2 px-4 text-body-2"
                >
                    <template v-slot:target>
                        <a
                            target="_blank"
                            rel="noopener noreferrer"
                            href="https://mariadb.com/kb/en/query-response-time-plugin/"
                        >
                            Query Response Time
                        </a>
                    </template>
                </i18n>
            </v-menu>
        </template>
        <!-- The height of the chart is 535 which equals to the height
             of the STATISTICS table. This makes the UI look balanced.
        -->
        <v-card outlined :height="535">
            <mxs-bar-chart
                ref="chart"
                class="fill-height"
                :chartData="chartData"
                :opts="chartOptions"
            />
        </v-card>
    </collapsible-ctr>
</template>

<script>
/*
 * Copyright (c) 2023 MariaDB plc
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
import { datasetObjectTooltip } from '@share/components/common/MxsCharts/customTooltips.js'

export default {
    name: 'res-time-dist-histogram',
    props: {
        resTimeDist: { type: Object, required: true },
    },
    data() {
        return {
            uniqueTooltipId: this.$helpers.lodash.uniqueId('tooltip_'),
        }
    },
    computed: {
        labels() {
            return this.$typy(this.resTimeDist, 'read.distribution').safeArray.map(
                item => item.time
            )
        },
        chartData() {
            return {
                datasets: [
                    {
                        label: 'Read',
                        data: this.$typy(this.resTimeDist, 'read.distribution').safeArray,
                        ...this.genDatasetStyleProperties(0),
                    },
                    {
                        label: 'Write',
                        data: this.$typy(this.resTimeDist, 'write.distribution').safeArray,
                        ...this.genDatasetStyleProperties(1),
                    },
                ],
            }
        },
        parsing() {
            return { xAxisKey: 'time', yAxisKey: 'count' }
        },
        chartOptions() {
            const scope = this
            return {
                layout: { padding: { left: 8, bottom: 8, right: 0, top: 8 } },
                parsing: this.parsing,
                scales: {
                    x: {
                        title: { display: true, text: 'Time (sec)', font: { size: 14 } },
                        ticks: { callback: tick => parseFloat(scope.labels[tick]) },
                    },
                    y: {
                        title: { display: true, text: 'Count' },
                        ticks: { callback: tick => tick },
                    },
                },
                plugins: {
                    legend: { display: true },
                    tooltip: {
                        external: context =>
                            datasetObjectTooltip({
                                context,
                                tooltipId: scope.uniqueTooltipId,
                                parsing: scope.parsing,
                                alignTooltipToLeft:
                                    context.tooltip.caretX >= scope.$refs.chart.$el.clientWidth / 2,
                            }),
                    },
                },
            }
        },
    },
    beforeDestroy() {
        this.removeTooltip()
    },
    methods: {
        removeTooltip() {
            let tooltipEl = document.getElementById(this.uniqueTooltipId)
            if (tooltipEl) tooltipEl.remove()
        },
        genDatasetStyleProperties(colorIdx = 0) {
            const lineColor = this.$helpers.dynamicColors(colorIdx)
            const indexOfOpacity = lineColor.lastIndexOf(')') - 1
            const backgroundColor = this.$helpers.strReplaceAt({
                str: lineColor,
                index: indexOfOpacity,
                newChar: '0.2',
            })
            return {
                backgroundColor,
                borderColor: lineColor,
                hoverBackgroundColor: lineColor,
                borderWidth: 1,
                minBarLength: 0,
            }
        },
    },
}
</script>

<style lang="scss">
.res-time-dist-histogram-info-tooltip-menu {
    background: $tooltip-background-color;
    opacity: 0.9;
    color: $tooltip-text-color;
}
</style>
