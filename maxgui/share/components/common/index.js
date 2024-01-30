/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
// Components to be shared between workspace and maxgui
import '@share/components/common/MxsCharts/config'
import MxsCnfField from '@share/components/common/MxsCnfField'
import MxsDataTable from '@share/components/common/MxsDataTable'
import MxsDlg from '@share/components/common/MxsDlg'
import MxsFilterList from '@share/components/common/MxsFilterList'
import MxsSelect from '@share/components/common/MxsSelect'
import MxsTooltipBtn from '@share/components/common/MxsTooltipBtn'
import MxsTruncateStr from '@share/components/common/MxsTruncateStr'
// ChartJs charts
import MxsBarChart from '@share/components/common/MxsCharts/MxsBarChart.vue'
import MxsLineChart from '@share/components/common/MxsCharts/MxsLineChart.vue'
import MxsScatterChart from '@share/components/common/MxsCharts/MxsScatterChart.vue'
// D3 graph components
import GraphBoard from '@share/components/common/MxsSvgGraphs/GraphBoard.vue'
import GraphNodes from '@share/components/common/MxsSvgGraphs/GraphNodes.vue'
import MxsStageCtr from '@share/components/common/MxsStageCtr'

export default {
    'mxs-cnf-field': MxsCnfField,
    'mxs-data-table': MxsDataTable,
    'mxs-dlg': MxsDlg,
    'mxs-filter-list': MxsFilterList,
    'mxs-select': MxsSelect,
    'mxs-tooltip-btn': MxsTooltipBtn,
    ...MxsTruncateStr,
    'mxs-bar-chart': MxsBarChart,
    'mxs-line-chart': MxsLineChart,
    'mxs-scatter-chart': MxsScatterChart,
    'mxs-svg-graph-board': GraphBoard,
    'mxs-svg-graph-nodes': GraphNodes,
    'mxs-stage-ctr': MxsStageCtr,
}
