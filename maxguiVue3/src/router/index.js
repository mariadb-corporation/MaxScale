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
import { createRouter, createWebHashHistory } from 'vue-router'
import { OVERLAY_LOGGING, OVERLAY_LOGOUT } from '@/constants/overlayTypes'
import { routes } from '@/router/routes'
import store from '@/store'

let router = createRouter({
  history: createWebHashHistory(),
  routes,
})

/**
 * @param {Number} type - overlay type
 */
async function showLoadingOverlay(type) {
  store.commit('mxsApp/SET_OVERLAY_TYPE', type, { root: true })
  await store.vue.$helpers
    .delay(400)
    .then(() => store.commit('mxsApp/SET_OVERLAY_TYPE', null, { root: true }))
}

/**
 * @param {Object} to - the target Route Object being navigated to.
 * @param {Object} from - the current route being navigated away from.
 * @param {Function} next - Function must be called to resolve the hook
 */
async function resolvingGuardedRoutes({ to, from, next }) {
  const {
    vue: { $typy },
    state,
  } = store
  const isAuthenticated = $typy(state, 'users.logged_in_user.isLoggedIn').safeBoolean
  if (isAuthenticated) {
    // show overlay loading screen after successfully authenticating
    if (from.name === 'login') {
      await showLoadingOverlay(OVERLAY_LOGGING)
    }
    next()
  } else {
    await showLoadingOverlay(OVERLAY_LOGOUT)
    next({
      path: '/login',
      query: { redirect: to.path },
    })
  }
}

const waitForStorageToBeReady = async (to, from, next) => {
  /* Make vuex-persist work with localForage by await store.restored
   *  https://github.com/championswimmer/vuex-persist#how-to-know-when-async-store-has-been-replaced
   */
  await store.restored
  store.commit('SET_PREV_ROUTE', from)
  const isGuardedRoutes = to.matched.some((record) => record.meta.requiresAuth)
  if (isGuardedRoutes) await resolvingGuardedRoutes({ to, from, next, store })
  else next()
}

router.beforeEach(waitForStorageToBeReady)
export default router
