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
  all_listeners: [],
  obj_data: {},
})
export default {
  namespaced: true,
  state: states(),
  mutations: genSetMutations(states()),
  actions: {
    async fetchAll({ commit }) {
      try {
        let res = await this.vue.$http.get(`/listeners`)
        if (res.data.data) commit('SET_ALL_LISTENERS', res.data.data)
      } catch (e) {
        this.vue.$logger.error(e)
      }
    },
  },
  getters: {
    total: (state) => state.all_listeners.length,
  },
}
