/*
 * Copyright (c) 2023 MariaDB plc
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-05-14
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import store from '@/store'
import { http } from '@/utils/axios'
import { t as typy } from 'typy'
import { globalI18n as i18n } from '@/plugins/i18n'
import { tryAsync } from '@/utils/helpers'

/**
 * @param {string} [filterParam]
 */
async function fetchSessions(filterParam) {
  const {
    pagination_config: { itemsPerPage, page },
  } = store.state.sessions
  const paginatedParams = `page[size]=${itemsPerPage}&page[number]=${page}`
  let params = []
  if (filterParam) params.push(filterParam)
  if (paginatedParams) params.push(paginatedParams)

  const url = '/sessions' + (params.length ? `?${params.join('&')}` : '')

  const [, res] = await tryAsync(http.get(url))

  const data = typy(res, 'data.data').safeArray
  const total = typy(res, 'data.meta.total').safeNumber

  store.commit('sessions/SET_CURRENT_SESSIONS', data)
  store.commit('sessions/SET_TOTAL_SESSIONS', total ? total : data.length)
}

/**
 * @param {string} param.id - id of the session
 * @param {function} param.callback callback function after successfully delete
 */
async function kill({ id, callback }) {
  const [, res] = await tryAsync(http.delete(`/sessions/${id}`))
  if (res.status === 200) {
    store.commit('mxsApp/SET_SNACK_BAR_MESSAGE', {
      text: [i18n.t('success.killedSessions')],
      type: 'success',
    })
    await typy(callback).safeFunction()
  }
}

export default { fetchSessions, kill }
