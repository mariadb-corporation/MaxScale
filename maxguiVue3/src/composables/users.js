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
import { t as typy } from 'typy'
import { tryAsync, delay } from '@/utils/helpers'
import router from '@/router'
import { USER_ADMIN_ACTIONS, PERSIST_TOKEN_OPT } from '@/constants'
import { OVERLAY_LOGOUT } from '@/constants/overlayTypes'
import { useFetchVersion } from '@/composables/maxscale'

export function useOpMap() {
  const { DELETE, UPDATE, ADD } = USER_ADMIN_ACTIONS
  const { t } = useI18n()
  const store = useStore()
  return {
    map: {
      [UPDATE]: {
        title: t(`userOps.actions.${UPDATE}`),
        type: UPDATE,
        icon: 'mxs:edit',
        iconSize: 18,
        color: 'primary',
      },
      [DELETE]: {
        title: t(`userOps.actions.${DELETE}`),
        type: DELETE,
        icon: ' mxs:delete',
        iconSize: 18,
        color: 'error',
      },
      [ADD]: {
        title: t(`userOps.actions.${ADD}`),
        type: ADD,
        color: 'primary',
      },
    },
    handler: async ({ type, payload, callback }) => {
      let promise
      let message
      switch (type) {
        case ADD:
          promise = http.post(`/users/inet`, {
            data: {
              id: payload.id,
              type: 'inet',
              attributes: { password: payload.password, account: payload.role },
            },
          })
          message = 'created'
          break
        case UPDATE:
          promise = http.patch(`/users/inet/${payload.id}`, {
            data: { attributes: { password: payload.password } },
          })
          message = 'updated'
          break
        case DELETE:
          promise = http.delete(`/users/inet/${payload.id}`)
          message = 'deleted'
          break
      }
      const [, res] = await tryAsync(promise)

      if (res.status === 204) {
        store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
          text: [`User ${payload.id} is ${message}`],
          type: 'success',
        })
        await typy(callback).safeFunction()
      }
    },
  }
}

export function useAuthCheck() {
  const store = useStore()
  let baseHttp = getBaseHttp()
  baseHttp.interceptors.response.use(
    (response) => response,
    async (error) => {
      const { response: { status = null } = {} } = error || {}
      if (status === 401) {
        abortRequests() // abort all requests created by baseHttp instance
        store.commit('users/SET_LOGGED_IN_USER', {})
        router.push('/login')
      }
    }
  )
  return async () => {
    const res = await baseHttp.get('/maxscale?fields[maxscale]=version')
    store.commit(
      'maxscale/SET_MAXSCALE_VERSION',
      typy(res, 'data.data.attributes.version').safeString
    )
  }
}

function useFetchAttrs() {
  const store = useStore()
  const logged_in_user = computed(() => store.state.users.logged_in_user)
  return async () => {
    /**
     * If the logged in user isn't an inet user, e.g. unix user or pam user, this returns 404.
     * Using authHttp so that it won't redirect to 404 page.
     */
    const [, res] = await tryAsync(authHttp.get(`/users/inet/${logged_in_user.value.name}`))
    store.commit('users/SET_LOGGED_IN_USER', {
      ...logged_in_user.value,
      attributes: typy(res, 'data.data.attributes').safeObjectOrEmpty,
    })
  }
}

export function useLogin() {
  const store = useStore()
  const { t } = useI18n()
  const fetchMxsVersion = useFetchVersion()
  const fetchUserAttrs = useFetchAttrs()
  return async ({ rememberMe, auth }) => {
    const url = rememberMe ? `/auth?${PERSIST_TOKEN_OPT}` : '/auth?persist=yes'
    const [e, res] = await tryAsync(authHttp.get(url, { auth }))
    if (e) {
      let errMsg = ''
      if (e.response)
        errMsg = e.response.status === 401 ? t('errors.wrongCredentials') : e.response.statusText
      else errMsg = e.toString()
      store.commit('users/SET_LOGIN_ERR_MSG', errMsg)
    } else if (res.status === 204) {
      store.commit('users/SET_LOGGED_IN_USER', {
        name: auth.username,
        rememberMe: rememberMe,
        isLoggedIn: true,
      })
      router.push(typy(router, 'currentRoute.value.redirectedFrom').safeString || '/')
      await fetchUserAttrs()
      await fetchMxsVersion()
    }
  }
}

export function useLogout() {
  const store = useStore()
  return async () => {
    store.commit('users/SET_LOGGED_IN_USER', {})
    store.commit('mxsApp/SET_OVERLAY_TYPE', OVERLAY_LOGOUT)
    const { snackbar_message } = store.state.mxsApp
    // hide snackbar snackbar_message if it is on
    if (snackbar_message.status) {
      store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
        text: snackbar_message.text,
        type: snackbar_message.type,
        status: false,
      })
    }
    await delay(1500).then(() => {
      store.commit('mxsApp/SET_OVERLAY_TYPE', null)
      if (typy(router, 'currentRoute.value.name').safeString !== 'login') router.push('/login')
    })
  }
}

export function useFetchAllNetworkUsers() {
  const store = useStore()
  return async () => {
    const [, res] = await tryAsync(http.get('/users/inet'))
    store.commit('users/SET_ALL_INET_USERS', typy(res, 'data.data').safeArray)
  }
}
