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

const states = () => ({
  maxscale_version: '',
  maxscale_overview_info: {},
  thread_stats: [],
  config_sync: null,
})

export default {
  namespaced: true,
  state: states(),
  mutations: genSetMutations(states()),
  actions: {
    async fetchVersion({ commit }) {
      const [, res] = await this.vue.$helpers.tryAsync(
        http.get('/maxscale?fields[maxscale]=version')
      )
      commit('SET_MAXSCALE_VERSION', this.vue.$typy(res, 'data.data.attributes.version').safeString)
    },

    async fetchConfigSync({ commit }) {
      const [, res] = await this.vue.$helpers.tryAsync(
        http.get('/maxscale?fields[maxscale]=config_sync')
      )
      commit('SET_CONFIG_SYNC', this.vue.$typy(res, 'data.data.attributes.config_sync').safeObject)
    },

    async fetchMaxScaleOverviewInfo({ commit }) {
      const [, res] = await this.vue.$helpers.tryAsync(
        http.get('/maxscale?fields[maxscale]=version,commit,started_at,activated_at,uptime')
      )
      commit(
        'SET_MAXSCALE_OVERVIEW_INFO',
        this.vue.$typy(res, 'data.data.attributes').safeObjectOrEmpty
      )
    },

    async fetchThreadStats({ commit }) {
      const [, res] = await this.vue.$helpers.tryAsync(
        http.get('/maxscale/threads?fields[threads]=stats')
      )
      commit('SET_THREAD_STATS', this.vue.$typy(res, 'data.data').safeArray)
    },
  },
}
