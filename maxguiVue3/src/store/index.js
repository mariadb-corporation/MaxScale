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
import { genSetMutations } from '@/utils/helpers'

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
    /**
     * This function fetch all resources data, if id is not provided,
     * @param {string} [param.id] id of the resource
     * @param {string} param.type type of resource. e.g. servers, services, monitors
     * @param {array} param.fields
     * @return {array|object} Resource data
     */
    async getResourceData(_, { type, id, fields = ['state'] }) {
      const { $helpers, $http, $typy } = this.vue
      let path = `/${type}`
      if (id) path += `/${id}`
      path += `?fields[${type}]=${fields.join(',')}`
      const [, res] = await $helpers.tryAsync($http.get(path))
      if (id) return $typy(res, 'data.data').safeObjectOrEmpty
      return $typy(res, 'data.data').safeArray
    },

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
})
