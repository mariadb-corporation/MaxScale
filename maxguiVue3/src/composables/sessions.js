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
import { http } from '@/utils/axios'
import { t as typy } from 'typy'
import { tryAsync } from '@/utils/helpers'

export function useFetchSessions() {
  const store = useStore()
  const paginatedParams = computed(() => {
    const {
      pagination_config: { itemsPerPage, page },
    } = store.state.sessions
    return `page[size]=${itemsPerPage}&page[number]=${page}`
  })
  /**
   * @param {string} [filterParam]
   */
  return async (filterParam) => {
    let params = []
    if (filterParam) params.push(filterParam)
    if (paginatedParams.value) params.push(paginatedParams.value)

    const url = '/sessions' + (params.length ? `?${params.join('&')}` : '')

    const [, res] = await tryAsync(http.get(url))

    const data = typy(res, 'data.data').safeArray
    const total = typy(res, 'data.meta.total').safeNumber

    store.commit('sessions/SET_CURRENT_SESSIONS', data)
    store.commit('sessions/SET_TOTAL_SESSIONS', total ? total : data.length)
  }
}

export function useKillSession() {
  const store = useStore()
  const { t } = useI18n()
  /**
   * @param {string} param.id - id of the session
   * @param {function} param.callback callback function after successfully delete
   */
  return async ({ id, callback }) => {
    const [, res] = await tryAsync(http.delete(`/sessions/${id}`))
    if (res.status === 200) {
      store.commit(
        'mxsApp/SET_SNACK_BAR_MESSAGE',
        { text: [t('success.killedSession')], type: 'success' },
        { root: true }
      )
      await typy(callback).safeFunction()
    }
  }
}
