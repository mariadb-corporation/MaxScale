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
    /**
     * @param {Object} payload payload object
     * @param {String} payload.id Name of the monitor
     * @param {String} payload.module The module to use
     * @param {Object} payload.parameters Parameters for the monitor
     * @param {Object} payload.relationships The relationships of the monitor to other resources
     * @param {Object} payload.relationships.servers severs relationships
     * @param {Function} payload.callback callback function after successfully updated
     */
    async create({ commit }, payload) {
      try {
        const body = {
          data: {
            id: payload.id,
            type: 'monitors',
            attributes: {
              module: payload.module,
              parameters: payload.parameters,
            },
            relationships: payload.relationships,
          },
        }
        let res = await this.vue.$http.post(`/monitors/`, body)
        let message = [`Monitor ${payload.id} is created`]
        // response ok
        if (res.status === 204) {
          commit(
            'mxsApp/SET_SNACK_BAR_MESSAGE',
            {
              text: message,
              type: 'success',
            },
            { root: true }
          )
          await this.vue.$typy(payload.callback).safeFunction()
        }
      } catch (e) {
        this.vue.$logger.error(e)
      }
    },
  },
  getters: {
    // -------------- below getters are available only when fetchAll has been dispatched
    total: (state) => state.all_monitors.length,
    monitorsMap: (state) => lodash.keyBy(state.all_monitors, 'id'),
  },
}
