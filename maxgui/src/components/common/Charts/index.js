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
import Chart from 'chart.js'
import LineChartStream from './LineChartStream.vue'
import './customTooltip.scss'

Chart.defaults.global.defaultFontFamily = "'azo-sans-web', adrianna, serif"
Chart.defaults.global.defaultFontColor = '#424F62'
Chart.defaults.global.defaultFontSize = 10

export default {
    'line-chart-stream': LineChartStream,
}
