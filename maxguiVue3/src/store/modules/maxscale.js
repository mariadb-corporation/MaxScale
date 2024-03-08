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
import { MXS_OBJ_TYPES } from '@/constants'
import { t } from 'typy'
import { genSetMutations, lodash } from '@/utils/helpers'

const states = () => ({
  maxscale_version: '',
  maxscale_overview_info: {},
  all_modules_map: {},
  thread_stats: [],
  maxscale_parameters: {},
  config_sync: null,
})

export default {
  namespaced: true,
  state: states(),
  mutations: genSetMutations(states()),
  actions: {
    async fetchVersion({ commit }) {
      const res = await this.vue.$http.get(`/maxscale?fields[maxscale]=version`)
      commit('SET_MAXSCALE_VERSION', this.vue.$typy(res, 'data.data.attributes.version').safeString)
    },

    async fetchMaxScaleParameters({ commit }) {
      try {
        let res = await this.vue.$http.get(`/maxscale?fields[maxscale]=parameters`)
        if (res.data.data.attributes.parameters)
          commit('SET_MAXSCALE_PARAMETERS', res.data.data.attributes.parameters)
      } catch (e) {
        this.vue.$logger.error(e)
      }
    },

    async fetchConfigSync({ commit }) {
      const [, res] = await this.vue.$helpers.tryAsync(
        this.vue.$http.get(`/maxscale?fields[maxscale]=config_sync`)
      )
      commit('SET_CONFIG_SYNC', this.vue.$typy(res, 'data.data.attributes.config_sync').safeObject)
    },

    async fetchMaxScaleOverviewInfo({ commit }) {
      try {
        let res = await this.vue.$http.get(
          `/maxscale?fields[maxscale]=version,commit,started_at,activated_at,uptime`
        )
        if (res.data.data.attributes) commit('SET_MAXSCALE_OVERVIEW_INFO', res.data.data.attributes)
      } catch (e) {
        this.vue.$logger.error(e)
      }
    },

    async fetchAllModules({ commit }) {
      const [, res] = await this.vue.$helpers.tryAsync(
        this.vue.$http.get('/maxscale/modules?load=all')
      )
      if (res.data.data) {
        commit(
          'SET_ALL_MODULES_MAP',
          this.vue.$helpers.lodash.groupBy(res.data.data, (item) => item.attributes.module_type)
        )
      }
    },

    async fetchThreadStats({ commit }) {
      try {
        let res = await this.vue.$http.get(`/maxscale/threads?fields[threads]=stats`)
        if (res.data.data) commit('SET_THREAD_STATS', res.data.data)
      } catch (e) {
        this.vue.$logger.error(e)
      }
    },

    /**
     * @param {Object} payload payload object
     * @param {String} payload.id maxscale
     * @param {Object} payload.parameters Parameters for the monitor
     * @param {Object} payload.callback callback function after successfully updated
     */
    async updateMaxScaleParameters({ commit }, payload) {
      try {
        const body = {
          data: {
            id: payload.id,
            type: 'maxscale',
            attributes: { parameters: payload.parameters },
          },
        }
        let res = await this.vue.$http.patch(`/maxscale`, body)
        // response ok
        if (res.status === 204) {
          commit(
            'mxsApp/SET_SNACK_BAR_MESSAGE',
            {
              text: [`MaxScale parameters is updated`],
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
    getMxsObjModules: (state) => (objType) => {
      const { SERVICES, SERVERS, MONITORS, LISTENERS, FILTERS } = MXS_OBJ_TYPES
      switch (objType) {
        case SERVICES:
          return t(state.all_modules_map['Router']).safeArray
        case SERVERS:
          return t(state.all_modules_map['servers']).safeArray
        case MONITORS:
          return t(state.all_modules_map['Monitor']).safeArray
        case FILTERS:
          return t(state.all_modules_map['Filter']).safeArray
        case LISTENERS: {
          let authenticators = t(state.all_modules_map['Authenticator']).safeArray.map(
            (item) => item.id
          )
          let protocols = lodash.cloneDeep(t(state.all_modules_map['Protocol']).safeArray || [])
          if (protocols.length) {
            protocols.forEach((protocol) => {
              protocol.attributes.parameters = protocol.attributes.parameters.filter(
                (o) => o.name !== 'protocol' && o.name !== 'service'
              )
              // Transform authenticator parameter from string type to enum type,
              let authenticatorParamObj = protocol.attributes.parameters.find(
                (o) => o.name === 'authenticator'
              )
              if (authenticatorParamObj) {
                authenticatorParamObj.type = 'enum'
                authenticatorParamObj.enum_values = authenticators
                // add default_value for authenticator
                authenticatorParamObj.default_value = ''
              }
            })
          }
          return protocols
        }
        default:
          return []
      }
    },
  },
}
