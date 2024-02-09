/*
 * Copyright (c) 2020 MariaDB Corporation Ab
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
import './config'
import MxsLineChartStream from './MxsLineChartStream.vue'
import MxsLineChart from './MxsLineChart.vue'
import MxsScatterChart from './MxsScatterChart.vue'
import MxsVertBarChart from './MxsVertBarChart.vue'
import MxsHorizBarChart from './MxsHorizBarChart.vue'
import MxsTreeGraph from './MxsTreeGraph.vue'
import MxsTreeGraphNode from './MxsTreeGraphNode.vue'
import MxsDagGraph from './MxsDagGraph.vue'

export default {
    'mxs-line-chart-stream': MxsLineChartStream,
    'mxs-line-chart': MxsLineChart,
    'mxs-scatter-chart': MxsScatterChart,
    'mxs-vert-bar-chart': MxsVertBarChart,
    'mxs-horiz-bar-chart': MxsHorizBarChart,
    'mxs-tree-graph': MxsTreeGraph, // d3 graph
    'mxs-tree-graph-node': MxsTreeGraphNode,
    'mxs-dag-graph': MxsDagGraph,
}
