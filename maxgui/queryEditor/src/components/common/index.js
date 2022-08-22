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
import '@share/components/common/Charts/config.js'
import LineChart from '@share/components/common/Charts/LineChart.vue'
import ScatterChart from '@share/components/common/Charts/ScatterChart.vue'
import VertBarChart from '@share/components/common/Charts/VertBarChart.vue'
import HorizBarChart from '@share/components/common/Charts/HorizBarChart.vue'
import SelectDropdown from '@share/components/common/SelectDropdown'
import Dialogs from '@share/components/common/Dialogs'
import Collapse from '@share/components/common/Collapse'
import SplitPane from '@share/components/common/SplitPane'
import MTreeVIew from '@share/components/common/MTreeView'
import VirtualScrollTable from '@share/components/common/VirtualScrollTable'
import TruncateString from '@share/components/common/TruncateString'
import SubMenu from '@share/components/common/SubMenu'
import FilterList from '@share/components/common/FilterList'

export default {
    'line-chart': LineChart,
    'scatter-chart': ScatterChart,
    'vert-bar-chart': VertBarChart,
    'horiz-bar-chart': HorizBarChart,
    ...Dialogs,
    'select-dropdown': SelectDropdown,
    collapse: Collapse,
    'split-pane': SplitPane,
    'm-treeview': MTreeVIew,
    'virtual-scroll-table': VirtualScrollTable,
    'truncate-string': TruncateString,
    'sub-menu': SubMenu,
    'filter-list': FilterList,
}
