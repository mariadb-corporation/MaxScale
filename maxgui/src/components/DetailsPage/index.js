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
import DetailsIconGroupWrapper from '@src/components/DetailsPage/DetailsIconGroupWrapper'
import DetailsPageTitle from '@src/components/DetailsPage/DetailsPageTitle'
import DetailsParametersTable from '@src/components/DetailsPage/DetailsParametersTable'
import DetailsReadonlyTable from '@src/components/DetailsPage/DetailsReadonlyTable'
import RelationshipTable from '@src/components/DetailsPage/RelationshipTable'
import RoutingTargetTable from '@src/components/DetailsPage/RoutingTargetTable'

export default {
    'details-icon-group-wrapper': DetailsIconGroupWrapper,
    'details-page-title': DetailsPageTitle,
    'details-parameters-table': DetailsParametersTable,
    'details-readonly-table': DetailsReadonlyTable,
    'relationship-table': RelationshipTable,
    'routing-target-table': RoutingTargetTable,
}
