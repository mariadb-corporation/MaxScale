/*
 * Copyright (c) 2023 MariaDB plc
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
import CollapsibleCtr from '@src/components/CollapsibleCtr'
import ConfirmDlg from '@src/components/ConfirmDlg'
import ConfNode from '@src/components/ConfNode'
import CreateMxsObj from '@src/components/CreateMxsObj'
import DagGraph from '@src/components/DagGraph'
import DataTable from '@src/components/DataTable'
import DateRangePicker from '@src/components/DateRangePicker'
import DetailsPage from '@src/components/DetailsPage'
import DurationDropdown from '@src/components/DurationDropdown'
import GlobalSearch from '@src/components/GlobalSearch'
import MonitorPageHeader from '@src/components/MonitorPageHeader'
import ObjectForms from '@src/components/ObjectForms'
import OutlinedOverviewCard from '@src/components/OutlinedOverviewCard'
import PageWrapper from '@src/components/PageWrapper'
import Parameters from '@src/components/Parameters'
import RefreshRate from '@src/components/RefreshRate'
import RepTooltip from '@src/components/RepTooltip'
import RoutingTargetSelect from '@src/components/RoutingTargetSelect'
import SelDlg from '@src/components/SelDlg'
import SessionsTable from '@src/components/SessionsTable'
import StatusIcon from '@src/components/StatusIcon'
import StreamLineChart from '@src/components/StreamLineChart'
import TreeGraph from '@src/components/TreeGraph'
import TreeGraphNode from '@src/components/TreeGraph/GraphNode.vue'
import shared from '@share/components/common'

export default {
    ...shared,
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
