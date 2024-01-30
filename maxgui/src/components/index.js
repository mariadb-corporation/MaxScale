/*
 * Copyright (c) 2023 MariaDB plc
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
import CollapsibleCtr from '@rootSrc/components/CollapsibleCtr'
import ConfirmDlg from '@rootSrc/components/ConfirmDlg'
import ConfNode from '@rootSrc/components/ConfNode'
import CreateMxsObj from '@rootSrc/components/CreateMxsObj'
import DagGraph from '@rootSrc/components/DagGraph'
import DataTable from '@rootSrc/components/DataTable'
import DateRangePicker from '@rootSrc/components/DateRangePicker'
import DetailsPage from '@rootSrc/components/DetailsPage'
import DurationDropdown from '@rootSrc/components/DurationDropdown'
import GlobalSearch from '@rootSrc/components/GlobalSearch'
import MonitorPageHeader from '@rootSrc/components/MonitorPageHeader'
import ObjectForms from '@rootSrc/components/ObjectForms'
import OutlinedOverviewCard from '@rootSrc/components/OutlinedOverviewCard'
import PageWrapper from '@rootSrc/components/PageWrapper'
import Parameters from '@rootSrc/components/Parameters'
import RefreshRate from '@rootSrc/components/RefreshRate'
import RepTooltip from '@rootSrc/components/RepTooltip'
import RoutingTargetSelect from '@rootSrc/components/RoutingTargetSelect'
import SelDlg from '@rootSrc/components/SelDlg'
import SessionsTable from '@rootSrc/components/SessionsTable'
import StatusIcon from '@rootSrc/components/StatusIcon'
import StreamLineChart from '@rootSrc/components/StreamLineChart'
import TreeGraph from '@rootSrc/components/TreeGraph'
import TreeGraphNode from '@rootSrc/components/TreeGraph/GraphNode.vue'
import shared from '@share/components/common'
import { workspaceComponents } from '@wsSrc/components/common'

export default {
    ...shared,
    ...workspaceComponents,
    'collapsible-ctr': CollapsibleCtr,
    'confirm-dlg': ConfirmDlg,
    'conf-node': ConfNode,
    'create-mxs-obj': CreateMxsObj,
    'dag-graph': DagGraph,
    'data-table': DataTable,
    'date-range-picker': DateRangePicker,
    ...DetailsPage,
    'duration-dropdown': DurationDropdown,
    'global-search': GlobalSearch,
    'monitor-page-header': MonitorPageHeader,
    ...ObjectForms,
    'outlined-overview-card': OutlinedOverviewCard,
    'page-wrapper': PageWrapper,
    ...Parameters,
    'refresh-rate': RefreshRate,
    'rep-tooltip': RepTooltip,
    'routing-target-select': RoutingTargetSelect,
    'sel-dlg': SelDlg,
    'sessions-table': SessionsTable,
    'status-icon': StatusIcon,
    'stream-line-chart': StreamLineChart,
    'tree-graph': TreeGraph,
    'tree-graph-node': TreeGraphNode,
}
