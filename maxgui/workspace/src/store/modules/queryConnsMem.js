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
import connection from '@wsSrc/api/connection'
import Worksheet from '@wsModels/Worksheet'
import base from '@wsSrc/api/base'

export default {
    namespaced: true,
    state: {
        is_validating_conn: true,
        conn_err_state: false,
        rc_target_names_map: {},
        pre_select_conn_rsrc: null,
        odbc_drivers: [],
    },
    mutations: {
        SET_IS_VALIDATING_CONN(state, payload) {
            state.is_validating_conn = payload
        },
        SET_CONN_ERR_STATE(state, payload) {
            state.conn_err_state = payload
        },
        SET_RC_TARGET_NAMES_MAP(state, payload) {
            state.rc_target_names_map = payload
        },
        SET_PRE_SELECT_CONN_RSRC(state, payload) {
            state.pre_select_conn_rsrc = payload
        },
        SET_ODBC_DRIVERS(state, payload) {
            state.odbc_drivers = payload
        },
    },
    actions: {
        async fetchRcTargetNames({ state, commit }, resourceType) {
            const config = Worksheet.getters('activeRequestConfig')
            const [e, res] = await this.vue.$helpers.to(
                base.get({ url: `/${resourceType}?fields[${resourceType}]=id`, config })
            )
            if (!e) {
                const names = this.vue
                    .$typy(res, 'data.data')
                    .safeArray.map(({ id, type }) => ({ id, type }))
                commit('SET_RC_TARGET_NAMES_MAP', {
                    ...state.rc_target_names_map,
                    [resourceType]: names,
                })
            }
        },
        async fetchOdbcDrivers({ commit }) {
            const config = Worksheet.getters('activeRequestConfig')
            const [e, res] = await this.vue.$helpers.to(connection.getDrivers(config))
            if (!e && res.status === 200) commit('SET_ODBC_DRIVERS', res.data.data)
        },
    },
}
