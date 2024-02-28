/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
// Components to be shared between workspace and maxgui
import '@share/components/common/MxsCharts/config'
import MxsCnfField from '@share/components/common/MxsCnfField'
import MxsDataTable from '@share/components/common/MxsDataTable'
import MxsDdlEditor from '@share/components/common/MxsDdlEditor'
import MxsDebouncedField from '@share/components/common/MxsDebouncedField'
import MxsDlg from '@share/components/common/MxsDlg'
import MxsFilterList from '@share/components/common/MxsFilterList'
import MxsLabelField from '@share/components/common/MxsLabelField'
import MxsLazyInput from '@share/components/common/MxsLazyInput'
import MxsPwdInput from '@share/components/common/MxsPwdInput'
import MxsSchemaNodeIcon from '@share/components/common/MxsSchemaNodeIcon'
import MxsSelect from '@share/components/common/MxsSelect'
import MxsSplitPane from '@share/components/common/MxsSplitPane'
import MxsSqlEditor from '@share/components/common/MxsSqlEditor'
import MxsStageCtr from '@share/components/common/MxsStageCtr'
import MxsSubMenu from '@share/components/common/MxsSubMenu'
import MxsTimeoutInput from '@share/components/common/MxsTimeoutInput/'
import MxsTooltipBtn from '@share/components/common/MxsTooltipBtn'
import MxsTreeview from '@share/components/common/MxsTreeview'
import MxsTruncateStr from '@share/components/common/MxsTruncateStr'
import MxsUidInput from '@share/components/common/MxsUidInput'
import MxsVirtualScrollTbl from '@share/components/common/MxsVirtualScrollTbl'
// ChartJs charts
import MxsBarChart from '@share/components/common/MxsCharts/MxsBarChart.vue'
import MxsLineChart from '@share/components/common/MxsCharts/MxsLineChart.vue'
import MxsScatterChart from '@share/components/common/MxsCharts/MxsScatterChart.vue'
// D3 graph components
import GraphBoard from '@share/components/common/MxsSvgGraphs/GraphBoard.vue'
import GraphNodes from '@share/components/common/MxsSvgGraphs/GraphNodes.vue'

export default {
    'mxs-cnf-field': MxsCnfField,
    'mxs-data-table': MxsDataTable,
    'mxs-ddl-editor': MxsDdlEditor,
    'mxs-debounced-field': MxsDebouncedField,
    'mxs-dlg': MxsDlg,
    'mxs-filter-list': MxsFilterList,
    'mxs-label-field': MxsLabelField,
    'mxs-lazy-input': MxsLazyInput,
    'mxs-pwd-input': MxsPwdInput,
    'mxs-schema-node-icon': MxsSchemaNodeIcon,
    'mxs-select': MxsSelect,
    'mxs-split-pane': MxsSplitPane,
    'mxs-sql-editor': MxsSqlEditor,
    'mxs-stage-ctr': MxsStageCtr,
    'mxs-sub-menu': MxsSubMenu,
    'mxs-timeout-input': MxsTimeoutInput,
    'mxs-tooltip-btn': MxsTooltipBtn,
    'mxs-treeview': MxsTreeview,
    ...MxsTruncateStr,
    'mxs-uid-input': MxsUidInput,
    'mxs-virtual-scroll-tbl': MxsVirtualScrollTbl,
    'mxs-bar-chart': MxsBarChart,
    'mxs-line-chart': MxsLineChart,
    'mxs-scatter-chart': MxsScatterChart,
    'mxs-svg-graph-board': GraphBoard,
    'mxs-svg-graph-nodes': GraphNodes,
}
