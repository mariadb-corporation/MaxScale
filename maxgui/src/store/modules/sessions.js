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
import { genSetMutations } from '@/utils/helpers'

const states = () => ({
  pagination_config: { page: 0, itemsPerPage: 20 },
  current_sessions: [],
  total_sessions: 0,
})

export default {
  namespaced: true,
  state: states(),
  mutations: genSetMutations(states()),
  getters: { total: (state) => state.total_sessions },
}
