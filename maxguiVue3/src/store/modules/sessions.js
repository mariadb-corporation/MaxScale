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
import { http } from '@/utils/axios'

const getDefPaginationConfig = () => ({
  page: 0,
  itemsPerPage: 20,
})

const states = () => ({
  pagination_config: getDefPaginationConfig(),
  current_sessions: [], //sessions on dashboard
  total_sessions: 0,
  filtered_sessions: [],
  total_filtered_sessions: 0,
})

export default {
  namespaced: true,
  state: states(),
  mutations: genSetMutations(states()),
  actions: {
    async fetchAll({ commit, getters }) {
      const {
        $helpers: { tryAsync },
        $typy,
      } = this.vue
      const paginateParam = getters.getPaginateParam
      const [, res] = await tryAsync(
        http.get(`/sessions${paginateParam ? `?${paginateParam}` : ''}`)
      )
      const data = $typy(res, 'data.data').safeArray
      commit('SET_CURRENT_SESSIONS', data)
      const total = $typy(res, 'data.meta.total').safeNumber
      commit('SET_TOTAL_SESSIONS', total ? total : data.length)
    },
    async fetchSessionsWithFilter({ getters, commit }, filterParam) {
      const {
        $helpers: { tryAsync },
        $typy,
      } = this.vue
      const paginateParam = getters.getPaginateParam
      const [, res] = await tryAsync(
        http.get(`/sessions?${filterParam}${paginateParam ? `&${paginateParam}` : ''}`)
      )
      const data = $typy(res, 'data.data').safeArray

      commit('SET_FILTERED_SESSIONS', data)
      const total = $typy(res, 'data.meta.total').safeNumber
      commit('SET_TOTAL_FILTERED_SESSIONS', total ? total : data.length)
    },
    /**
     * @param {String} param.id - id of the session
     * @param {Function} param.callback callback function after successfully delete
     */
    async killSession({ commit }, { id, callback }) {
      const {
        $helpers: { tryAsync },
        $typy,
        $t,
      } = this.vue

      const [, res] = await tryAsync(http.delete(`/sessions/${id}`))
      if (res.status === 200) {
        commit(
          'mxsApp/SET_SNACK_BAR_MESSAGE',
          { text: [$t('success.killedSession')], type: 'success' },
          { root: true }
        )
        await $typy(callback).safeFunction()
      }
    },
  },
  getters: {
    total: (state) => state.total_sessions,
    getTotalFilteredSessions: (state) => state.total_filtered_sessions,
    getPaginateParam: ({ pagination_config: { itemsPerPage, page } }) =>
      itemsPerPage === -1 ? '' : `page[size]=${itemsPerPage}&page[number]=${page}`,
    getFilterParamByServiceId: () => (serviceId) =>
      `filter=/relationships/services/data/0/id="${serviceId}"`,
  },
}
