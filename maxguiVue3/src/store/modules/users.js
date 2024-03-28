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
import { http, authHttp, getBaseHttp, abortRequests } from '@/utils/axios'
import { PERSIST_TOKEN_OPT, USER_ROLES } from '@/constants'
import { OVERLAY_LOGOUT } from '@/constants/overlayTypes'
import { genSetMutations } from '@/utils/helpers'
import router from '@/router'

const states = () => ({
  logged_in_user: {},
  login_err_msg: '',
  all_inet_users: [],
})

export default {
  namespaced: true,
  state: states(),
  mutations: {
    CLEAR_USER(state) {
      state.logged_in_user = null
    },
    ...genSetMutations(states()),
  },
  actions: {
    // To be called before app is mounted
    async authCheck({ commit }) {
      let http = getBaseHttp()
      http.interceptors.response.use(
        (response) => response,
        async (error) => {
          const { response: { status = null } = {} } = error || {}
          if (status === 401) {
            abortRequests() // abort all requests created by http instance
            commit('CLEAR_USER')
            router.push('/login')
          }
        }
      )
      const res = await http.get('/maxscale?fields[maxscale]=version')
      commit(
        'maxscale/SET_MAXSCALE_VERSION',
        this.vue.$typy(res, 'data.data.attributes.version').safeString,
        { root: true }
      )
    },
    async login({ commit, dispatch }, { rememberMe, auth }) {
      const url = rememberMe ? `/auth?${PERSIST_TOKEN_OPT}` : '/auth?persist=yes'
      const [e, res] = await this.vue.$helpers.tryAsync(authHttp.get(url, { auth }))
      if (e) {
        let errMsg = ''
        if (e.response)
          errMsg =
            e.response.status === 401
              ? this.vue.$t('errors.wrongCredentials')
              : e.response.statusText
        else errMsg = e.toString()
        commit('SET_LOGIN_ERR_MSG', errMsg)
      } else if (res.status === 204) {
        commit('SET_LOGGED_IN_USER', {
          name: auth.username,
          rememberMe: rememberMe,
          isLoggedIn: true,
        })
        router.push(this.vue.$typy(router, 'currentRoute.value.redirectedFrom').safeString || '/')
        await dispatch('fetchLoggedInUserAttrs')
        await dispatch('maxscale/fetchVersion', {}, { root: true })
      }
    },
    async logout({ commit, rootState }) {
      commit('CLEAR_USER')
      commit('mxsApp/SET_OVERLAY_TYPE', OVERLAY_LOGOUT, { root: true })
      const { snackbar_message } = rootState.mxsApp
      // hide snackbar snackbar_message if it is on
      if (snackbar_message.status) {
        commit(
          'mxsApp/SET_SNACK_BAR_MESSAGE',
          { text: snackbar_message.text, type: snackbar_message.type, status: false },
          { root: true }
        )
      }
      await this.vue.$helpers.delay(1500).then(() => {
        commit('mxsApp/SET_OVERLAY_TYPE', null, { root: true })
        if (this.vue.$typy(router, 'currentRoute.value.name').safeString !== 'login')
          router.push('/login')
      })
    },
    // ------------------------------------------------ Inet (network) users ---------------------------------
    async fetchLoggedInUserAttrs({ commit, state }) {
      try {
        /**
         * If the logged in user isn't an inet user, e.g. unix user or pam user, this returns 404.
         * Using authHttp so that it won't redirect to 404 page.
         */
        const res = await authHttp.get(`/users/inet/${state.logged_in_user.name}`)
        // response ok
        if (res.status === 200)
          commit('SET_LOGGED_IN_USER', {
            ...state.logged_in_user,
            attributes: res.data.data.attributes,
          })
      } catch (e) {
        this.vue.$logger.error(e)
      }
    },
    async fetchAllNetworkUsers({ commit }) {
      const {
        $helpers: { tryAsync },
        $typy,
      } = this.vue
      const [, res] = await tryAsync(http.get('/users/inet'))
      commit('SET_ALL_INET_USERS', $typy(res, 'data.data').safeArray)
    },
  },
  getters: {
    getLoggedInUserRole: (state) => {
      const { attributes: { account = '' } = {} } = state.logged_in_user || {}
      return account
    },
    isAdmin: (state, getters) => {
      return getters.getLoggedInUserRole === USER_ROLES.ADMIN
    },
  },
}