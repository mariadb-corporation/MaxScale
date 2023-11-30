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
import ConfNode from '@share/components/common/ConfNode'
import CreateMxsObj from '@share/components/common/CreateMxsObj'
import DataTable from '@share/components/common/DataTable'
import DurationDropdown from '@share/components/common/DurationDropdown'
import DetailsPage from '@share/components/common/DetailsPage'
import GlobalSearch from '@share/components/common/GlobalSearch'
import IconSpriteSheet from '@share/components/common/IconSpriteSheet'
import MonitorPageHeader from '@share/components/common/MonitorPageHeader'
import MxsCollapse from '@share/components/common/MxsCollapse'
import MxsConfDlg from '@share/components/common/MxsConfDlg'
import MxsSelDlg from '@share/components/common/MxsSelDlg'
import MxsLineChartStream from '@share/components/common/MxsCharts/MxsLineChartStream.vue'
import MxsDagGraph from '@share/components/common/MxsSvgGraphs/MxsDagGraph.vue'
import MxsTreeGraph from '@share/components/common/MxsSvgGraphs/MxsTreeGraph.vue'
import MxsTreeGraphNode from '@share/components/common/MxsSvgGraphs/MxsTreeGraphNode.vue'
import ObjectForms from '@share/components/common/ObjectForms'
import PageWrapper from '@share/components/common/PageWrapper'
import Parameters from '@share/components/common/Parameters'
import RepTooltip from '@share/components/common/RepTooltip'
import RefreshRate from '@share/components/common/RefreshRate'
import RoutingTargetSelect from '@share/components/common/RoutingTargetSelect'
import SessionsTable from '@share/components/common/SessionsTable'
import OutlinedOverviewCard from '@share/components/common/OutlinedOverviewCard'

import shared from '@share/components/common/shared'
import { workspaceComponents } from '@wsSrc/components/common'

export default {
    ...shared,
    ...workspaceComponents,
    'conf-node': ConfNode,
    'create-mxs-obj': CreateMxsObj,
    'data-table': DataTable,
    'duration-dropdown': DurationDropdown,
    ...DetailsPage,
    'global-search': GlobalSearch,
    'icon-sprite-sheet': IconSpriteSheet,
    'monitor-page-header': MonitorPageHeader,
    'mxs-collapse': MxsCollapse,
    'mxs-conf-dlg': MxsConfDlg,
    'mxs-sel-dlg': MxsSelDlg,
    'mxs-line-chart-stream': MxsLineChartStream,
    'mxs-dag-graph': MxsDagGraph,
    'mxs-tree-graph': MxsTreeGraph,
    'mxs-tree-graph-node': MxsTreeGraphNode,
    ...ObjectForms,
    'page-wrapper': PageWrapper,
    ...Parameters,
    'rep-tooltip': RepTooltip,
    'refresh-rate': RefreshRate,
    'routing-target-select': RoutingTargetSelect,
    'sessions-table': SessionsTable,
    'outlined-overview-card': OutlinedOverviewCard,
}
