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
import QueryTab from '@wsModels/QueryTab'

export default {
    namespaced: true,
    getters: {
        activeRecord: () => QueryTab.getters('activeRecord').insightViewer || {},
        activeNode: (state, getters) => getters.activeRecord.active_node || {},
        activeSpec: (state, getters) => getters.activeRecord.active_spec,
    },
}
