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
import { APP_CONFIG } from '@rootSrc/utils/constants'
// Place here any states need to be persisted without being cleared when logging out
export default {
    namespaced: true,
    state: {
        refresh_rate_by_route_group: APP_CONFIG.DEF_REFRESH_RATE_BY_GROUP,
    },
    mutations: {
        UPDATE_REFRESH_RATE_BY_ROUTE_GROUP(state, { group, payload }) {
            state.refresh_rate_by_route_group[group] = payload
        },
    },
}
