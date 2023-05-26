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
import '@share/components/common/MxsCharts/config'
import MxsLineChartStream from '@share/components/common/MxsCharts/MxsLineChartStream.vue'
import MxsLineChart from '@share/components/common/MxsCharts/MxsLineChart.vue'
import MxsScatterChart from '@share/components/common/MxsCharts/MxsScatterChart.vue'
import MxsBarChart from '@share/components/common/MxsCharts/MxsBarChart.vue'

export default {
    'mxs-line-chart-stream': MxsLineChartStream,
    'mxs-line-chart': MxsLineChart,
    'mxs-scatter-chart': MxsScatterChart,
    'mxs-bar-chart': MxsBarChart,
}
