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
import { USER_ADMIN_ACTIONS } from '@/constants'

export function useUserOpMap() {
  const { DELETE, UPDATE, ADD } = USER_ADMIN_ACTIONS
  const { t } = useI18n()
  const { tryAsync } = useHelpers()
  const store = useStore()
  const http = useHttp()
  const typy = useTypy()
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
