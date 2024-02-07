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
import { immutableUpdate } from '@/utils/helpers'

export default {
  namespaced: true,
  state: {
    abort_controller_map: {},
  },
  mutations: {
    UPDATE_ABORT_CONTROLLER_MAP(state, { id, value }) {
      state.abort_controller_map = immutableUpdate(state.abort_controller_map, {
        [id]: { $set: value },
      })
    },
    DELETE_ABORT_CONTROLLER(state, id) {
      this.vue.$delete(state.abort_controller_map, id)
    },
  },
  getters: {
    getAbortController: (state) => (id) => state.abort_controller_map[id],
  },
}
