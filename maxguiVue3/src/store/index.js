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
import { createStore } from 'vuex'
import modules from '@/store/modules'
import plugins from '@/store/plugins'
import router from '@/router'
import { genSetMutations } from '@/utils/helpers'
import { t as typy } from 'typy'

const states = () => ({
  search_keyword: '',
  prev_route: null,
  module_parameters: [],
  form_type: '',
  should_refresh_resource: false,
})

export default createStore({
  plugins: plugins,
  state: states(),
  mutations: genSetMutations(states()),
  actions: {
    async fetchModuleParameters({ commit }, moduleId) {
      const { $helpers, $http } = this.vue
      let data = []
      const [, res] = await $helpers.tryAsync(
        $http.get(`/maxscale/modules/${moduleId}?fields[modules]=parameters`)
      )
      if (res.data.data) {
        const { attributes: { parameters = [] } = {} } = res.data.data
        data = parameters
      }
      commit('SET_MODULE_PARAMETERS', data)
    },
  },
  modules,
  getters: {
    currRefreshRate: (state, getters, rootState) => {
      const group = typy(router, 'currentRoute.value.meta.group').safeString
      if (group) return rootState.persisted.refresh_rate_by_route_group[group]
      return 10
    },
  },
})
