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
import '@share/components/common/MxsCharts/config.js'
import MxsDataTable from '@share/components/common/MxsDataTable'
import MxsDlgs from '@share/components/common/MxsDlgs'
import MxsLineChart from '@share/components/common/MxsCharts/MxsLineChart.vue'
import MxsScatterChart from '@share/components/common/MxsCharts/MxsScatterChart.vue'
import MxsVertBarChart from '@share/components/common/MxsCharts/MxsVertBarChart.vue'
import MxsHorizBarChart from '@share/components/common/MxsCharts/MxsHorizBarChart.vue'
import MxsCollapse from '@share/components/common/MxsCollapse'
import MxsFilterList from '@share/components/common/MxsFilterList'
import MxsSelect from '@share/components/common/MxsSelect'
import MxsSplitPane from '@share/components/common/MxsSplitPane'
import MxsSubMenu from '@share/components/common/MxsSubMenu'
import MxsTooltipBtn from '@share/components/common/MxsTooltipBtn'
import MxsTreeview from '@share/components/common/MxsTreeview'
import MxsTruncateStr from '@share/components/common/MxsTruncateStr'
import MxsTxtFieldWithLabel from '@share/components/common/MxsTxtFieldWithLabel'
import MxsVirtualScrollTbl from '@share/components/common/MxsVirtualScrollTbl'
import PageWrapper from '@share/components/common/PageWrapper'

export default {
    'mxs-data-table': MxsDataTable,
    ...MxsDlgs,
    'mxs-line-chart': MxsLineChart,
    'mxs-scatter-chart': MxsScatterChart,
    'mxs-vert-bar-chart': MxsVertBarChart,
    'mxs-horiz-bar-chart': MxsHorizBarChart,
    'mxs-collapse': MxsCollapse,
    'mxs-filter-list': MxsFilterList,
    'mxs-select': MxsSelect,
    'mxs-split-pane': MxsSplitPane,
    'mxs-sub-menu': MxsSubMenu,
    'mxs-tooltip-btn': MxsTooltipBtn,
    'mxs-treeview': MxsTreeview,
    ...MxsTruncateStr,
    'mxs-txt-field-with-label': MxsTxtFieldWithLabel,
    'mxs-virtual-scroll-tbl': MxsVirtualScrollTbl,
    'page-wrapper': PageWrapper,
}
