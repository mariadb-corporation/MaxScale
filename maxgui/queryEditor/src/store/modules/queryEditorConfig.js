/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-10-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

import * as config from '../config'
export default {
    namespaced: true,
    state: {
        config,
        hidden_comp: [''],
        axios_opts: {},
        auth_cookies_max_age: 86400,
    },
    mutations: {
        SET_HIDDEN_COMP(state, payload) {
            state.hidden_comp = payload
        },
        SET_AXIOS_OPTS(state, payload) {
            state.axios_opts = payload
        },
        SET_AUTH_COOKIES_MAX_AGE(state, payload) {
            state.auth_cookies_max_age = payload
        },
    },
}
