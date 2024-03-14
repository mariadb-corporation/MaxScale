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
import { genSetMutations, lodash } from '@/utils/helpers'

const states = () => ({ all_monitors: [], obj_data: {}, monitor_diagnostics: {} })

export default {
  namespaced: true,
  state: states(),
  mutations: genSetMutations(states()),
  actions: {
    async fetchAll({ commit }) {
      try {
        let res = await this.vue.$http.get(`/monitors`)
        if (res.data.data) commit('SET_ALL_MONITORS', res.data.data)
      } catch (e) {
        this.vue.$logger.error(e)
      }
    },
    async fetchDiagnostics({ commit }, id) {
      const [, res] = await this.vue.$helpers.tryAsync(
        this.vue.$http.get(`/monitors/${id}?fields[monitors]=monitor_diagnostics`)
      )
      commit(
        'SET_MONITOR_DIAGNOSTICS',
        this.vue.$typy(res, 'data.data.attributes.monitor_diagnostics').safeObjectOrEmpty
      )
    },
  },
  getters: {
    total: (state) => state.all_monitors.length,
    monitorsMap: (state) => lodash.keyBy(state.all_monitors, 'id'),
  },
}
