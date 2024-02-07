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
import { authHttp, getBaseHttp, abortRequests } from '@/utils/axios'
import { PERSIST_TOKEN_OPT, USER_ROLES, USER_ADMIN_ACTIONS } from '@/constants'
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
            abortRequests() // abort all requests created by $http instance
            commit('CLEAR_USER')
            router.push('/login')
          }
        }
      )
      const res = await http.get('/api/maxscale?fields[maxscale]=version')
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
              ? this.vue.$mxs_t('errors.wrongCredentials')
              : e.response.statusText
        else errMsg = e.toString()
        commit('SET_LOGIN_ERR_MSG', errMsg)
      } else if (res.status === 204) {
        commit('SET_LOGGED_IN_USER', {
          name: auth.username,
          rememberMe: rememberMe,
          isLoggedIn: true,
        })
        router.push(
          this.vue.$typy(router, 'currentRoute.value.redirectedFrom').safeString ||
            '/dashboard/servers'
        )
        await dispatch('fetchLoggedInUserAttrs')
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
      try {
        const res = await this.vue.$http.get(`/users/inet`)
        // response ok
        if (res.status === 200) commit('SET_ALL_INET_USERS', res.data.data)
      } catch (e) {
        this.vue.$logger.error(e)
      }
    },
    /**Only admin accounts can perform POST, PUT, DELETE and PATCH requests
     * @param {String} payload.mode - add, update or delete
     * @param {String} payload.id - inet user id. Required for all modes
     * @param {String} payload.password - inet user's password. Required for mode `add` or `update`
     * @param {String} payload.role - admin or basic. Required for mode `post`
     * @param {Function} payload.callback - callback function after receiving 204 (response ok)
     */
    async manageInetUser({ commit }, payload) {
      try {
        let res
        let message
        const { ADD, UPDATE, DELETE } = USER_ADMIN_ACTIONS
        switch (payload.mode) {
          case ADD:
            res = await this.vue.$http.post(`/users/inet`, {
              data: {
                id: payload.id,
                type: 'inet',
                attributes: { password: payload.password, account: payload.role },
              },
            })
            message = [`Network User ${payload.id} is created`]
            break
          case UPDATE:
            res = await this.vue.$http.patch(`/users/inet/${payload.id}`, {
              data: {
                attributes: { password: payload.password },
              },
            })
            message = [`Network User ${payload.id} is updated`]
            break
          case DELETE:
            res = await this.vue.$http.delete(`/users/inet/${payload.id}`)
            message = [`Network user ${payload.id} is deleted`]
            break
        }
        if (res.status === 204) {
          commit('mxsApp/SET_SNACK_BAR_MESSAGE', { text: message, type: 'success' }, { root: true })
          await this.vue.$typy(payload.callback).safeFunction()
        }
      } catch (e) {
        this.vue.$logger.error(e)
      }
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
    getUserAdminActions: () => {
      const { DELETE, UPDATE, ADD } = USER_ADMIN_ACTIONS
      // scope is needed to access $mxs_t
      return ({ scope }) => ({
        [UPDATE]: {
          text: scope.$mxs_t(`userOps.actions.${UPDATE}`),
          type: UPDATE,
          icon: '$vuetify.icons.mxs_edit',
          iconSize: 18,
          color: 'primary',
        },
        [DELETE]: {
          text: scope.$mxs_t(`userOps.actions.${DELETE}`),
          type: DELETE,
          icon: ' $vuetify.icons.mxs_delete',
          iconSize: 18,
          color: 'error',
        },
        [ADD]: {
          text: scope.$mxs_t(`userOps.actions.${ADD}`),
          type: ADD,
          color: 'primary',
        },
      })
    },
  },
}
