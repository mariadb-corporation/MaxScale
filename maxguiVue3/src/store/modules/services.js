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
  all_services: [],
  obj_data: {},
})

export default {
  namespaced: true,
  state: states(),
  mutations: genSetMutations(states()),
  actions: {
    async fetchAll({ commit }) {
      try {
        let res = await this.vue.$http.get(`/services`)
        if (res.data.data) commit('SET_ALL_SERVICES', res.data.data)
      } catch (e) {
        this.vue.$logger.error(e)
      }
    },
    /**
     * @param {Object} payload payload object
     * @param {String} payload.id Name of the service
     * @param {String} payload.module The router module to use
     * @param {Object} payload.parameters Parameters for the service
     * @param {Object} payload.relationships The relationships of the service to other resources
     * @param {Object} payload.relationships.servers servers object
     * @param {Object} payload.relationships.filters filters object
     * @param {Function} payload.callback callback function after successfully updated
     */
    async create({ commit }, payload) {
      try {
        const body = {
          data: {
            id: payload.id,
            type: 'services',
            attributes: {
              router: payload.module,
              parameters: payload.parameters,
            },
            relationships: payload.relationships,
          },
        }
        let res = await this.vue.$http.post(`/services/`, body)

        // response ok
        if (res.status === 204) {
          commit(
            'mxsApp/SET_SNACK_BAR_MESSAGE',
            {
              text: [`Service ${payload.id} is created`],
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
    total: (state) => state.all_services.length,
  },
}
