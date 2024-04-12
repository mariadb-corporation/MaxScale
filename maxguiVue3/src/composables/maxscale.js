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

export function useFetchVersion() {
  const store = useStore()
  return async () => {
    const [, res] = await tryAsync(http.get('/maxscale?fields[maxscale]=version'))
    store.commit(
      'maxscale/SET_MAXSCALE_VERSION',
      typy(res, 'data.data.attributes.version').safeString
    )
  }
}

export function useFetchConfigSync() {
  const store = useStore()
  return async () => {
    const [, res] = await tryAsync(http.get('/maxscale?fields[maxscale]=config_sync'))
    store.commit(
      'maxscale/SET_CONFIG_SYNC',
      typy(res, 'data.data.attributes.config_sync').safeObject
    )
  }
}

export function useFetchOverviewInfo() {
  const store = useStore()
  return async () => {
    const [, res] = await tryAsync(
      http.get('/maxscale?fields[maxscale]=version,commit,started_at,activated_at,uptime')
    )
    store.commit(
      'maxscale/SET_MAXSCALE_OVERVIEW_INFO',
      typy(res, 'data.data.attributes').safeObjectOrEmpty
    )
  }
}

export function useFetchThreadStats() {
  const store = useStore()
  return async () => {
    const [, res] = await tryAsync(http.get('/maxscale/threads?fields[threads]=stats'))
    store.commit('maxscale/SET_THREAD_STATS', typy(res, 'data.data').safeArray)
  }
}
