/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { OVERLAY_LOGOUT } from 'store/overlayTypes'

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
        logout(state) {
            state.user = null
        },
    },
    actions: {
        async logout({ commit, rootState }) {
            if (this.router.app.$route.path) {
                commit('logout')
                commit('showOverlay', OVERLAY_LOGOUT, { root: true })
                const user = JSON.parse(localStorage.getItem('user'))
                if (user) {
                    localStorage.removeItem('user')
                }
                commit('setUser', {})
                this.Vue.prototype.$help.deleteCookie('token_body')
                // hide snackbar message if it is on
                if (rootState.message.status) {
                    commit(
                        'showMessage',
                        {
                            text: rootState.message.text,
                            type: rootState.message.type,
                            status: false,
                        },
                        { root: true }
                    )
                }

                await this.Vue.prototype.$help.delay(1500).then(() => {
                    return commit('hideOverlay', null, { root: true }), this.router.push('/login')
                })
            }
        },
        // --------------------------------------------------- Network users -------------------------------------
        async fetchCurrentNetworkUser({ dispatch, commit, state }) {
            let res = await this.Vue.axios.get(`/users/inet/${state.user.username}`)
            // response ok
            if (res.status === 200) {
                let data = res.data.data
                commit('setCurrentNetworkUser', data)
                if (data.attributes.account === 'admin') {
                    await dispatch('fetchAllNetworkUsers')
                    await dispatch('fetchAllUNIXAccounts')
                    await dispatch('fetchAllUsers')
                }
            }
        },
        async fetchAllNetworkUsers({ commit }) {
            let res = await this.Vue.axios.get(`/users/inet`)
            // response ok
            if (res.status === 200) {
                commit('setAllNetworkUsers', res.data.data)
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
                        res = await this.Vue.axios.post(`/users/inet`, payload)
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
                        res = await this.Vue.axios.patch(`/users/inet/${data.id}`, payload)
                        message = [`Network User ${data.id} is updated`]
                    }
                    break
            }
            // response ok
            if (res.status === 204) {
                commit(
                    'showMessage',
                    {
                        text: message,
                        type: 'success',
                    },
                    { root: true }
                )
                await dispatch('fetchAllNetworkUsers')
            }
        },
        /**
         * @param {String} id id of the network user
         */
        async deleteNetworkUserById({ dispatch, commit }, id) {
            let res = await this.Vue.axios.delete(`/users/inet/${id}`)
            // response ok
            if (res.status === 204) {
                await dispatch('fetchAllNetworkUsers')
                commit(
                    'showMessage',
                    {
                        text: [`Network user ${id} is deleted`],
                        type: 'success',
                    },
                    { root: true }
                )
            }
        },
        // --------------------------------------------------- Unix accounts -------------------------------------
        async fetchAllUNIXAccounts({ commit }) {
            let res = await this.Vue.axios.get(`/users/unix`)
            // response ok
            if (res.status === 200) {
                commit('setAllUNIXAccounts', res.data.data)
            }
        },
        async enableUNIXAccount({ commit, dispatch }, { id, role }) {
            let res = await this.Vue.axios.get(`/users/unix`, {
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
                    'showMessage',
                    {
                        text: [`UNIX account ${id} is enabled`],
                        type: 'success',
                    },
                    { root: true }
                )
                await dispatch('fetchAllUNIXAccounts')
            }
        },
        /**
         * @param {String} id id of the UNIX user
         */
        async disableUNIXAccount({ dispatch, commit }, id) {
            let res = await this.Vue.axios.delete(`/users/unix/${id}`)
            // response ok
            if (res.status === 204) {
                await dispatch('fetchAllUNIXAccounts')
                commit(
                    'showMessage',
                    {
                        text: [`UNIX account ${id} is disabled`],
                        type: 'success',
                    },
                    { root: true }
                )
            }
        },
        // --------------------------------------------------- All users -----------------------------------------
        async fetchAllUsers({ commit }) {
            let res = await this.Vue.axios.get(`/users`)
            // response ok
            if (res.status === 200) {
                commit('setAllUsers', res.data.data)
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
