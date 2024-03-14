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
import connection from '@/api/sql/connection'
import Worksheet from '@wsModels/Worksheet'
import { genSetMutations, tryAsync } from '@/utils/helpers'

const states = () => ({
  is_validating_conn: true,
  conn_err_state: false,
  pre_select_conn_item: null,
  odbc_drivers: [],
})

export default {
  namespaced: true,
  state: states(),
  mutations: genSetMutations(states()),
  actions: {
    async fetchOdbcDrivers({ commit }) {
      const config = Worksheet.getters('activeRequestConfig')
      const [e, res] = await tryAsync(connection.getDrivers(config))
      if (!e && res.status === 200) commit('SET_ODBC_DRIVERS', res.data.data)
    },
  },
}
