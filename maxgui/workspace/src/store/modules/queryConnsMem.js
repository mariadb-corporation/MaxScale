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
import connection from '@wsSrc/api/connection'
import Worksheet from '@wsModels/Worksheet'
import base from '@wsSrc/api/base'
import { genSetMutations } from '@share/utils/helpers'

const states = () => ({
    is_validating_conn: true,
    conn_err_state: false,
    rc_target_names_map: {},
    pre_select_conn_rsrc: null,
    odbc_drivers: [],
})

export default {
    namespaced: true,
    state: states(),
    mutations: genSetMutations(states()),
    actions: {
        async fetchRcTargetNames({ state, commit }, resourceType) {
            const config = Worksheet.getters('activeRequestConfig')
            const [e, res] = await this.vue.$helpers.tryAsync(
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
            const [e, res] = await this.vue.$helpers.tryAsync(connection.getDrivers(config))
            if (!e && res.status === 200) commit('SET_ODBC_DRIVERS', res.data.data)
        },
    },
}
