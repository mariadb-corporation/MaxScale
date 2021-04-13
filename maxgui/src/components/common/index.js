/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-03-24
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import PageWrapper from './PageWrapper'
import SelectDropdown from './SelectDropdown'
import DataTable from './DataTable'
import Dialogs from './Dialogs'
import IconSpriteSheet from './IconSpriteSheet'
import OutlinedOverviewCard from './OutlinedOverviewCard'
import Collapse from './Collapse'

import LineChart from './LineChart.vue'
import GlobalSearch from './GlobalSearch'
import CreateResource from './CreateResource'

import DetailsPage from './DetailsPage'
import Parameters from './Parameters'
import SplitPane from './SplitPane'

export default {
    'page-wrapper': PageWrapper,
    'global-search': GlobalSearch,
    'create-resource': CreateResource,
    ...Dialogs,
    'select-dropdown': SelectDropdown,
    'data-table': DataTable,
    'icon-sprite-sheet': IconSpriteSheet,
    'outlined-overview-card': OutlinedOverviewCard,
    collapse: Collapse,
    ...Parameters,
    'line-chart': LineChart,
    ...DetailsPage,
    'split-pane': SplitPane,
}
