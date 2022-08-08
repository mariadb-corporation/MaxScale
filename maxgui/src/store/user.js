/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-08-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { OVERLAY_LOGOUT } from 'store/overlayTypes'
import localForage from 'localforage'
export default {
    namespaced: true,
    state: {
        logged_in_user: {},
        login_err_msg: '',
        all_inet_users: [],
    },
    mutations: {
        /**
         * @param {Object} userObj User rememberMe info
         * @param {Boolean} userObj.rememberMe rememberMe
         * @param {String} userObj.name username
         */
        SET_LOGGED_IN_USER(state, userObj) {
            state.logged_in_user = userObj
        },
        SET_LOGIN_ERR_MSG(state, errMsg) {
            state.login_err_msg = errMsg
        },
        CLEAR_USER(state) {
            state.logged_in_user = null
        },
        // ------------------- maxscale users
        SET_ALL_INET_USERS(state, arr) {
            state.all_inet_users = arr
        },
    },
    actions: {
        async login({ commit, dispatch }, { rememberMe, auth }) {
            try {
                /* Using $authHttp instance, instead of using $http as it's configured to have global interceptor*/
                this.$refreshAxiosToken()
                let url = '/auth?persist=yes'
                let res = await this.$authHttp.get(`${url}${rememberMe ? '&max-age=86400' : ''}`, {
                    auth,
                })
                if (res.status === 204) {
                    commit('SET_LOGGED_IN_USER', {
                        name: auth.username,
                        rememberMe: rememberMe,
                        isLoggedIn: Boolean(this.vue.$help.getCookie('token_body')),
                    })
                    this.router.push(this.router.app.$route.query.redirect || '/dashboard/servers')
                    await dispatch('fetchLoggedInUserAttrs')
                }
            } catch (e) {
                let errMsg = ''
                if (e.response) {
                    errMsg =
                        e.response.status === 401
                            ? this.i18n.t('errors.wrongCredentials')
                            : e.response.statusText
                } else {
                    const logger = this.vue.$logger('store-user-login')
                    logger.error(e)
                    errMsg = e.toString()
                }
                commit('SET_LOGIN_ERR_MSG', errMsg)
            }
        },
        async logout({ commit, dispatch, rootState }) {
            await dispatch('queryConn/disconnectAll', {}, { root: true })
            this.$cancelAllRequests() // cancel all previous requests before logging out
            commit('CLEAR_USER')
            commit('SET_OVERLAY_TYPE', OVERLAY_LOGOUT, { root: true })

            // hide snackbar snackbar_message if it is on
            if (rootState.snackbar_message.status) {
                commit(
                    'SET_SNACK_BAR_MESSAGE',
                    {
                        text: rootState.snackbar_message.text,
                        type: rootState.snackbar_message.type,
                        status: false,
                    },
                    { root: true }
                )
            }
            await this.vue.$help.delay(1500).then(() => {
                commit('SET_OVERLAY_TYPE', null, { root: true })
                if (this.router.app.$route.name !== 'login') this.router.push('/login')
            })

            // Clear all but persist some states of some modules
            const persistedState = this.vue.$help.lodash.cloneDeep({
                persisted: rootState.persisted,
                wke: {
                    worksheets_arr: rootState.wke.worksheets_arr,
                },
                querySession: {
                    active_session_by_wke_id_map:
                        rootState.querySession.active_session_by_wke_id_map,
                    query_sessions: rootState.querySession.query_sessions,
                },
            })
            await localForage.clear()
            this.vue.$help.deleteAllCookies()
            await localForage.setItem('maxgui', persistedState)
        },
        // ------------------------------------------------ Inet (network) users ---------------------------------
        async fetchLoggedInUserAttrs({ commit, state }) {
            try {
                const res = await this.$http.get(`/users/inet/${state.logged_in_user.name}`)
                // response ok
                if (res.status === 200)
                    commit('SET_LOGGED_IN_USER', {
                        ...state.logged_in_user,
                        attributes: res.data.data.attributes,
                    })
            } catch (e) {
                const logger = this.vue.$logger('store-user-fetchLoggedInUserAttrs')
                logger.error(e)
            }
        },
        async fetchAllNetworkUsers({ commit }) {
            try {
                const res = await this.$http.get(`/users/inet`)
                // response ok
                if (res.status === 200) commit('SET_ALL_INET_USERS', res.data.data)
            } catch (e) {
                const logger = this.vue.$logger('store-user-fetchAllNetworkUsers')
                logger.error(e)
            }
        },
        /**Only admin accounts can perform POST, PUT, DELETE and PATCH requests
         * @param {String} payload.mode - add, update or delete
         * @param {String} payload.id - inet user id. Required for all modes
         * @param {String} payload.password - inet user's password. Required for mode `add` or `update`
         * @param {String} payload.role - admin or basic. Required for mode `post`
         * @param {Function} payload.callback - callback function after receiving 204 (response ok)
         */
        async manageInetUser({ commit, rootState }, payload) {
            try {
                let res
                let message
                const { ADD, UPDATE, DELETE } = rootState.app_config.USER_ADMIN_ACTIONS
                switch (payload.mode) {
                    case ADD:
                        res = await this.$http.post(`/users/inet`, {
                            data: {
                                id: payload.id,
                                type: 'inet',
                                attributes: { password: payload.password, account: payload.role },
                            },
                        })
                        message = [`Network User ${payload.id} is created`]
                        break
                    case UPDATE:
                        res = await this.$http.patch(`/users/inet/${payload.id}`, {
                            data: {
                                attributes: { password: payload.password },
                            },
                        })
                        message = [`Network User ${payload.id} is updated`]
                        break
                    case DELETE:
                        res = await this.$http.delete(`/users/inet/${payload.id}`)
                        message = [`Network user ${payload.id} is deleted`]
                        break
                }
                if (res.status === 204) {
                    commit(
                        'SET_SNACK_BAR_MESSAGE',
                        { text: message, type: 'success' },
                        { root: true }
                    )
                    if (this.vue.$help.isFunction(payload.callback)) await payload.callback()
                }
            } catch (e) {
                const logger = this.vue.$logger('store-user-manageInetUser')
                logger.error(e)
            }
        },
    },
    getters: {
        getLoggedInUserRole: state => {
            const { attributes: { account = '' } = {} } = state.logged_in_user || {}
            return account
        },
        isAdmin: (state, getters, rootState) => {
            return getters.getLoggedInUserRole === rootState.app_config.USER_ROLES.ADMIN
        },
        getUserAdminActions: (state, getters, rootState) => {
            const { DELETE, UPDATE, ADD } = rootState.app_config.USER_ADMIN_ACTIONS
            // scope is needed to access $t
            return ({ scope }) => ({
                [UPDATE]: {
                    text: scope.$t(`userOps.actions.${UPDATE}`),
                    type: UPDATE,
                    icon: '$vuetify.icons.edit',
                    iconSize: 18,
                    color: 'primary',
                },
                [DELETE]: {
                    text: scope.$t(`userOps.actions.${DELETE}`),
                    type: DELETE,
                    icon: ' $vuetify.icons.delete',
                    iconSize: 18,
                    color: 'error',
                },
                [ADD]: {
                    text: scope.$t(`userOps.actions.${ADD}`),
                    type: ADD,
                    color: 'accent-dark',
                },
            })
        },
    },
}
