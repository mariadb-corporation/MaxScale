/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-12-13
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Vue from 'vue'
import Router from 'vue-router'
import { routes } from './routes'
import { OVERLAY_LOADING } from 'store/overlayTypes'
import store from 'store'

Vue.use(Router)

let router = new Router({
    /*
    To use history mode, the web server needs to configure to serve it
    https://router.vuejs.org/guide/essentials/history-mode.html
   */
    // mode: 'history',
    routes: routes,
})
router.beforeEach(async (to, from, next) => {
    store.commit('SET_PREV_ROUTE', from)
    const isGuardedRoutes = to.matched.some(record => record.meta.requiresAuth)
    if (isGuardedRoutes) await resolvingGuardedRoutes(to, from, next)
    else await Promise.all([store.dispatch('checkingForUpdate'), next()])
})
export default router

/**
 * @param {Number} duration - duration time for showing overlay loading
 */
async function showLoadingOverlay(duration) {
    store.commit('SET_OVERLAY_TYPE', OVERLAY_LOADING)
    await store.vue.$help.delay(duration).then(() => store.commit('SET_OVERLAY_TYPE', null))
}
/**
 *
 *
 * @param {Object} to - the target Route Object being navigated to.
 * @param {Object} from - the current route being navigated away from.
 * @param {Function} next - Function must be called to resolve the hook
 */
async function resolvingGuardedRoutes(to, from, next) {
    // Check if user is logged in
    const { getCookie } = store.vue.$help
    const user = JSON.parse(localStorage.getItem('user'))
    const isAuthenticated = getCookie('token_body') && user.isLoggedIn

    if (isAuthenticated) {
        // show overlay loading screen after successfully authenticating
        if (from.name === 'login') {
            await showLoadingOverlay(1500)
        }
        next()
    } else {
        await showLoadingOverlay(600)
        next({
            path: '/login',
            query: { redirect: to.path },
        })
    }
}
