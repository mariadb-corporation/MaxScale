/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-08-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import './config'
import LineChartStream from './LineChartStream.vue'
import LineChart from './LineChart.vue'
import ScatterChart from './ScatterChart.vue'
import VertBarChart from './VertBarChart.vue'
import HorizBarChart from './HorizBarChart.vue'
import TreeGraph from './TreeGraph.vue'
import TreeGraphNode from './TreeGraphNode.vue'
import DagGraph from './DagGraph.vue'

export default {
    'line-chart-stream': LineChartStream,
    'line-chart': LineChart,
    'scatter-chart': ScatterChart,
    'vert-bar-chart': VertBarChart,
    'horiz-bar-chart': HorizBarChart,
    'tree-graph': TreeGraph, // d3 graph
    'tree-graph-node': TreeGraphNode,
    'dag-graph': DagGraph,
}
