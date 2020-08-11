/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-07-16
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
        user: JSON.parse(localStorage.getItem('user')),
        currentNetworkUser: null,
        allNetworkUsers: [],
        allUNIXAccounts: [],
        allUsers: [],
    },
    mutations: {
        /**
         * @param {Object} userObj User rememberMe info
         * @param {Boolean} userObj.rememberMe rememberMe
         * @param {String} userObj.name username
         */
        setUser(state, userObj) {
            state.user = userObj
        },
        // ------------------- Network users
        setCurrentNetworkUser(state, obj) {
            state.currentNetworkUser = obj
        },
        setAllNetworkUsers(state, arr) {
            state.allNetworkUsers = arr
        },
        // ------------------- Unix accounts
        setAllUNIXAccounts(state, arr) {
            state.allUNIXAccounts = arr
        },
        // ------------------- All users
        setAllUsers(state, arr) {
            state.allUsers = arr
        },
        clearUser(state) {
            state.user = null
        },
    },
    actions: {
        async logout({ commit, rootState }) {
            cancelAllRequests() // cancel all previous requests before logging out
            commit('clearUser')
            commit('SET_OVERLAY_TYPE', OVERLAY_LOGOUT, { root: true })
            const user = JSON.parse(localStorage.getItem('user'))
            if (user) localStorage.removeItem('user')
            this.vue.$help.deleteCookie('token_body')
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
                let res = await this.vue.$axios.get(`/users/inet/${state.user.username}`)
                // response ok
                if (res.status === 200 && res.data.data) {
                    let data = res.data.data
                    commit('setCurrentNetworkUser', data)
                    if (data.attributes.account === 'admin') {
                        await dispatch('fetchAllNetworkUsers')
                        await dispatch('fetchAllUNIXAccounts')
                        await dispatch('fetchAllUsers')
                    }
                }
            } catch (e) {
                if (process.env.NODE_ENV !== 'test') {
                    const logger = this.vue.$logger('store-user-fetchCurrentNetworkUser')
                    logger.error(e)
                }
            }
        },
        async fetchAllNetworkUsers({ commit }) {
            try {
                let res = await this.vue.$axios.get(`/users/inet`)
                // response ok
                if (res.status === 200 && res.data.data) {
                    commit('setAllNetworkUsers', res.data.data)
                }
            } catch (e) {
                if (process.env.NODE_ENV !== 'test') {
                    const logger = this.vue.$logger('store-user-fetchAllNetworkUsers')
                    logger.error(e)
                }
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
                if (process.env.NODE_ENV !== 'test') {
                    const logger = this.vue.$logger('store-user-createOrUpdateNetworkUser')
                    logger.error(e)
                }
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
                if (process.env.NODE_ENV !== 'test') {
                    const logger = this.vue.$logger('store-user-deleteNetworkUserById')
                    logger.error(e)
                }
            }
        },
        // --------------------------------------------------- Unix accounts -------------------------------------
        async fetchAllUNIXAccounts({ commit }) {
            try {
                let res = await this.vue.$axios.get(`/users/unix`)
                // response ok
                if (res.status === 200 && res.data.data) {
                    commit('setAllUNIXAccounts', res.data.data)
                }
            } catch (e) {
                if (process.env.NODE_ENV !== 'test') {
                    const logger = this.vue.$logger('store-user-fetchAllUNIXAccounts')
                    logger.error(e)
                }
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
                if (process.env.NODE_ENV !== 'test') {
                    const logger = this.vue.$logger('store-user-enableUNIXAccount')
                    logger.error(e)
                }
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
                if (process.env.NODE_ENV !== 'test') {
                    const logger = this.vue.$logger('store-user-disableUNIXAccount')
                    logger.error(e)
                }
            }
        },
        // --------------------------------------------------- All users -----------------------------------------
        async fetchAllUsers({ commit }) {
            try {
                let res = await this.vue.$axios.get(`/users`)
                // response ok
                if (res.status === 200 && res.data.data) {
                    commit('setAllUsers', res.data.data)
                }
            } catch (e) {
                if (process.env.NODE_ENV !== 'test') {
                    const logger = this.vue.$logger('store-user-fetchAllUsers')
                    logger.error(e)
                }
            }
        },
    },
    getters: {
        user: state => state.user,
        currentNetworkUser: state => state.currentNetworkUser,
        allUsers: state => state.allUsers,

        allUNIXAccounts: state => state.allUNIXAccounts,
    },
}
