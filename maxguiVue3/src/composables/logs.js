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
import { tryAsync } from '@/utils/helpers'
import { t as typy } from 'typy'

export function useFetchLatest() {
  const store = useStore()
  return async () => {
    const [, res] = await tryAsync(
      http.get(`/maxscale/logs/entries?${store.getters['logs/logFilters']}`)
    )
    const { data = [], links: { prev = '' } = {} } = res.data
    store.commit('logs/SET_LATEST_LOGS', Object.freeze(data))
    const logSource = typy(data, '[0].attributes.log_source').safeString
    if (logSource) store.commit('logs/SET_LOG_SOURCE', logSource)
    store.commit('logs/SET_PREV_LOG_LINK', prev)
  }
}

export function useFetchPrev() {
  const store = useStore()
  return async () => {
    const [, res] = await tryAsync(
      http.get(`/maxscale/logs/entries?${store.getters['logs/prevLogsParams']}`)
    )
    const {
      data,
      links: { prev = '' },
    } = res.data
    store.commit('logs/SET_PREV_LOGS', Object.freeze(data))
    store.commit('logs/SET_PREV_LOG_LINK', prev)
  }
}
