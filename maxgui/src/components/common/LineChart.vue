<script>
/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { Line } from 'vue-chartjs'
import 'chartjs-plugin-streaming'
export default {
    extends: Line,
    props: {
        chartData: {
            type: Object,
        },
        options: {
            type: Object,
        },
        isRealTime: { type: Boolean, default: true },

        yAxesTicks: {
            type: Object,
            default: () => {},
        },
    },
    data() {
        return {
            realtimeScales: {
                xAxes: [
                    {
                        gridLines: {
                            lineWidth: 0.6,
                            color: 'rgba(234, 234, 234, 1)',
                            drawTicks: false,
                            drawBorder: true,
                            zeroLineColor: 'rgba(234, 234, 234, 1)',
                        },
                        type: 'realtime',
                        ticks: {
                            display: false,
                        },
                    },
                ],
                yAxes: [
                    {
                        gridLines: {
                            lineWidth: 0.6,
                            color: 'rgba(234, 234, 234,1)',
                            drawTicks: false,
                            drawBorder: false,
                            zeroLineColor: 'transparent',
                        },
                        ticks: {
                            beginAtZero: true,
                            padding: 12,
                            fontSize: 10,
                            fontFamily: "'azo-sans-web', adrianna, serif",
                            fontColor: '#424F62',
                            maxTicksLimit: 3,
                            ...this.yAxesTicks,
                        },
                    },
                ],
            },
            defaultScales: {
                xAxes: [
                    {
                        display: true,
                        ticks: {
                            fontSize: 10,
                            fontColor: '#424F62',
                            fontFamily: "'azo-sans-web', adrianna, serif",
                        },
                    },
                ],
                yAxes: [
                    {
                        display: true,
                        ticks: {
                            fontSize: 10,
                            fontFamily: "'azo-sans-web', adrianna, serif",
                            fontColor: '#424F62',
                            padding: 0,
                        },
                    },
                ],
            },
            realtimeLayout: {
                padding: {
                    left: 2,
                    bottom: 10,
                    right: 0,
                    top: 15,
                },
            },
            defaultLayout: {
                padding: {
                    left: 0,
                    right: 0,
                    top: 0,
                    bottom: 0,
                },
            },
            uniqueTooltipId: this.$help.lodash.uniqueId('tooltip_'),
        }
    },
    watch: {
        /* This chartData watcher doesn't make the chart reactivity, but it helps to
        destroy the chart when it's unmounted from the page. Eg: moving from dashboard page (have 3 charts)
        to service-detail page (1 chart), the chart will be destroyed and rerender to avoid 
        several problems within vue-chartjs while using chartjs-plugin-streaming
        */
        chartData: function() {
            this.$data._chart.destroy()
            this.renderLineChart()
        },
    },
    beforeDestroy() {
        let tooltipEl = document.getElementById(this.uniqueTooltipId)
        tooltipEl && tooltipEl.remove()
        if (this.$data._chart) this.$data._chart.destroy()
    },
    mounted() {
        this.renderLineChart()
    },
    methods: {
        renderLineChart() {
            let self = this
            this.renderChart(this.chartData, {
                showLines: true,

                layout: self.isRealTime ? self.realtimeLayout : self.defaultLayout,
                legend: {
                    display: false,
                },
                responsive: true,
                maintainAspectRatio: false,
                elements: {
                    point: {
                        radius: 0,
                    },
                },
                hover: {
                    mode: 'index',
                    intersect: false,
                },

                tooltips: {
                    mode: 'x-axis',
                    intersect: false,
                    titleFontFamily: "'azo-sans-web', adrianna, serif",
                    bodyFontFamily: "'azo-sans-web', adrianna, serif",

                    enabled: false,
                    custom: function(tooltipModel) {
                        // Tooltip Element

                        let tooltipEl = document.getElementById(self.uniqueTooltipId)

                        // Create element on first render
                        if (!tooltipEl) {
                            tooltipEl = document.createElement('div')
                            tooltipEl.id = self.uniqueTooltipId
                            tooltipEl.className = ['chartjs-tooltip shadow-drop']
                            tooltipEl.innerHTML = '<table></table>'
                            document.body.appendChild(tooltipEl)
                        }

                        // Hide if no tooltip
                        if (tooltipModel.opacity === 0) {
                            tooltipEl.style.opacity = 0
                            return
                        }

                        // Set caret Position
                        tooltipEl.classList.remove('above', 'below', 'no-transform')
                        if (tooltipModel.yAlign) {
                            tooltipEl.classList.add(tooltipModel.yAlign)
                        } else {
                            tooltipEl.classList.add('no-transform')
                        }

                        function getBody(bodyItem) {
                            return bodyItem.lines
                        }

                        // Set Text
                        if (tooltipModel.body) {
                            let titleLines = tooltipModel.title || []
                            let bodyLines = tooltipModel.body.map(getBody)

                            let innerHtml = '<thead>'

                            titleLines.forEach(function(title) {
                                innerHtml += '<tr><th>' + title + '</th></tr>'
                            })
                            innerHtml += '</thead><tbody>'

                            bodyLines.forEach(function(body, i) {
                                let colors = tooltipModel.labelColors[i]
                                let style = 'background:' + colors.backgroundColor
                                style += '; border-color:' + colors.borderColor
                                style += '; border-width: 2px;margin-right:4px'
                                let span =
                                    '<span class="chartjs-tooltip-key" style="' +
                                    style +
                                    '"></span>'
                                innerHtml += '<tr><td>' + span + body + '</td></tr>'
                            })
                            innerHtml += '</tbody>'

                            let tableRoot = tooltipEl.querySelector('table')
                            tableRoot.innerHTML = innerHtml
                        }

                        // `this` will be the overall tooltip
                        let chart = this._chart.canvas.getBoundingClientRect()

                        // Display, position, and set styles for font
                        tooltipEl.style.opacity = 1
                        // this makes sure the tooltip wont go over client view width when realtime chart is used
                        let left =
                            chart.left + tooltipModel.caretX > chart.width + chart.left
                                ? chart.width + chart.left
                                : chart.left + tooltipModel.caretX

                        tooltipEl.style.left = left + 'px'
                        tooltipEl.style.top = chart.top + tooltipModel.caretY + 'px'
                        tooltipEl.style.fontFamily = tooltipModel._bodyFontFamily
                        tooltipEl.style.fontStyle = tooltipModel._bodyFontStyle
                        tooltipEl.style.padding =
                            tooltipModel.yPadding + 'px ' + tooltipModel.xPadding + 'px'
                    },
                },

                scales: this.isRealTime ? this.realtimeScales : this.defaultScales,
                ...this.options,
            })
        },
    },
}
</script>

<style lang="scss">
.chartjs-tooltip {
    opacity: 1;
    position: absolute;
    background-color: #ffffff;
    border-color: #ffffff;
    color: rgba(0, 0, 0, 0.87);
    border-radius: 10px;
    transition: all 0.3s ease;
    pointer-events: none;
    transform: translate(-50%, 0);
    th,
    td {
        font-size: 10px;
    }
}
.chartjs-tooltip-key {
    display: inline-block;
    width: 10px;
    height: 10px;
}
</style>
