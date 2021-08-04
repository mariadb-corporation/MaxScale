/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-07-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// Place here any states need to be persisted without being cleared when logging out
export default {
    namespaced: true,
    state: {
        // QUery toolbar's states
        query_max_rows: 10000,
        query_confirm_flag: 1,
    },
    mutations: {
        SET_QUERY_MAX_ROW(state, payload) {
            state.query_max_rows = payload
        },
        SET_QUERY_CONFIRM_FLAG(state, payload) {
            state.query_confirm_flag = payload // payload is either 0 or 1
        },
    },
}
