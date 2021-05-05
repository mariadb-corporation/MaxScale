/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-04-28
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { OVERLAY_LOGOUT } from 'store/overlayTypes'
import { cancelAllRequests } from 'plugins/axios'

export default {
    namespaced: true,
    state: {
        logged_in_user: JSON.parse(localStorage.getItem('user')),
        current_network_user: null,
        all_network_users: [],
        all_unix_accounts: [],
        all_users: [],
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
        // ------------------- Network users
        SET_CURRENT_NETWORK_USER(state, obj) {
            state.current_network_user = obj
        },
        SET_ALL_NETWORK_USERS(state, arr) {
            state.all_network_users = arr
        },
        // ------------------- Unix accounts
        SET_ALL_UNIX_ACCOUNTS(state, arr) {
            state.all_unix_accounts = arr
        },
        // ------------------- All users
        SET_ALL_USERS(state, arr) {
            state.all_users = arr
        },
        CLEAR_USER(state) {
            state.logged_in_user = null
        },
    },
    actions: {
        async logout({ commit, rootState }) {
            cancelAllRequests() // cancel all previous requests before logging out
            commit('CLEAR_USER')
            commit('SET_OVERLAY_TYPE', OVERLAY_LOGOUT, { root: true })
            localStorage.clear()
            sessionStorage.clear()
            this.vue.$help.deleteAllCookies()
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
        },
        // --------------------------------------------------- Network users -------------------------------------
        async fetchCurrentNetworkUser({ dispatch, commit, state }) {
            try {
                let res = await this.vue.$axios.get(`/users/inet/${state.logged_in_user.username}`)
                // response ok
                if (res.status === 200 && res.data.data) {
                    let data = res.data.data
                    commit('SET_CURRENT_NETWORK_USER', data)
                    if (data.attributes.account === 'admin') {
                        await dispatch('fetchAllNetworkUsers')
                        await dispatch('fetchAllUNIXAccounts')
                        await dispatch('fetchAllUsers')
                    }
                }
            } catch (e) {
                const logger = this.vue.$logger('store-user-fetchCurrentNetworkUser')
                logger.error(e)
            }
        },
        async fetchAllNetworkUsers({ commit }) {
            try {
                let res = await this.vue.$axios.get(`/users/inet`)
                // response ok
                if (res.status === 200 && res.data.data) {
                    commit('SET_ALL_NETWORK_USERS', res.data.data)
                }
            } catch (e) {
                const logger = this.vue.$logger('store-user-fetchAllNetworkUsers')
                logger.error(e)
            }
        },
        /**Only admin accounts can perform POST, PUT, DELETE and PATCH requests
         * @param {Object} data data object
         * @param {String} data.mode Mode to perform async request POST or Patch
         * @param {String} data.id the username
         * @param {String} data.password The password for this user
         * @param {String} data.role Set to admin for administrative users and basic to read-only users
         */
        async createOrUpdateNetworkUser({ commit, dispatch }, data) {
            try {
                let res
                let message
                switch (data.mode) {
                    case 'post':
                        {
                            const payload = {
                                data: {
                                    id: data.id,
                                    type: 'inet',
                                    attributes: { password: data.password, account: data.role },
                                },
                            }
                            res = await this.vue.$axios.post(`/users/inet`, payload)
                            message = [`Network User ${data.id} is created`]
                        }
                        break
                    case 'patch':
                        {
                            const payload = {
                                data: {
                                    attributes: { password: data.password },
                                },
                            }
                            res = await this.vue.$axios.patch(`/users/inet/${data.id}`, payload)
                            message = [`Network User ${data.id} is updated`]
                        }
                        break
                }
                // response ok
                if (res.status === 204) {
                    commit(
                        'SET_SNACK_BAR_MESSAGE',
                        {
                            text: message,
                            type: 'success',
                        },
                        { root: true }
                    )
                    await dispatch('fetchAllNetworkUsers')
                }
            } catch (e) {
                const logger = this.vue.$logger('store-user-createOrUpdateNetworkUser')
                logger.error(e)
            }
        },
        /**
         * @param {String} id id of the network user
         */
        async deleteNetworkUserById({ dispatch, commit }, id) {
            try {
                let res = await this.vue.$axios.delete(`/users/inet/${id}`)
                // response ok
                if (res.status === 204) {
                    await dispatch('fetchAllNetworkUsers')
                    commit(
                        'SET_SNACK_BAR_MESSAGE',
                        {
                            text: [`Network user ${id} is deleted`],
                            type: 'success',
                        },
                        { root: true }
                    )
                }
            } catch (e) {
                const logger = this.vue.$logger('store-user-deleteNetworkUserById')
                logger.error(e)
            }
        },
        // --------------------------------------------------- Unix accounts -------------------------------------
        async fetchAllUNIXAccounts({ commit }) {
            try {
                let res = await this.vue.$axios.get(`/users/unix`)
                // response ok
                if (res.status === 200 && res.data.data) {
                    commit('SET_ALL_UNIX_ACCOUNTS', res.data.data)
                }
            } catch (e) {
                const logger = this.vue.$logger('store-user-fetchAllUNIXAccounts')
                logger.error(e)
            }
        },
        async enableUNIXAccount({ commit, dispatch }, { id, role }) {
            try {
                let res = await this.vue.$axios.get(`/users/unix`, {
                    data: {
                        id: id,
                        type: 'unix',
                        attributes: {
                            account: role,
                        },
                    },
                })
                // response ok
                if (res.status === 204) {
                    commit(
                        'SET_SNACK_BAR_MESSAGE',
                        {
                            text: [`UNIX account ${id} is enabled`],
                            type: 'success',
                        },
                        { root: true }
                    )
                    await dispatch('fetchAllUNIXAccounts')
                }
            } catch (e) {
                const logger = this.vue.$logger('store-user-enableUNIXAccount')
                logger.error(e)
            }
        },
        /**
         * @param {String} id id of the UNIX user
         */
        async disableUNIXAccount({ dispatch, commit }, id) {
            try {
                let res = await this.vue.$axios.delete(`/users/unix/${id}`)
                // response ok
                if (res.status === 204) {
                    await dispatch('fetchAllUNIXAccounts')
                    commit(
                        'SET_SNACK_BAR_MESSAGE',
                        {
                            text: [`UNIX account ${id} is disabled`],
                            type: 'success',
                        },
                        { root: true }
                    )
                }
            } catch (e) {
                const logger = this.vue.$logger('store-user-disableUNIXAccount')
                logger.error(e)
            }
        },
        // --------------------------------------------------- All users -----------------------------------------
        async fetchAllUsers({ commit }) {
            try {
                let res = await this.vue.$axios.get(`/users`)
                // response ok
                if (res.status === 200 && res.data.data) {
                    commit('SET_ALL_USERS', res.data.data)
                }
            } catch (e) {
                const logger = this.vue.$logger('store-user-fetchAllUsers')
                logger.error(e)
            }
        },
    },
}
