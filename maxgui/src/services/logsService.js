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
import store from '@/store'
import { http } from '@/utils/axios'
import { tryAsync } from '@/utils/helpers'
import { t as typy } from 'typy'

function getData(res) {
  return typy(res, 'data.data').safeArray
}

function getPrevLink(res) {
  return typy(res, 'data.links.prev').safeString
}

async function fetchLatest() {
  const [, res] = await tryAsync(
    http.get(`/maxscale/logs/entries?${store.getters['logs/logFilters']}`)
  )
  const data = getData(res)
  store.commit('logs/SET_LATEST_LOGS', Object.freeze(data))
  const logSource = typy(data, '[0].attributes.log_source').safeString
  if (logSource) store.commit('logs/SET_LOG_SOURCE', logSource)
  store.commit('logs/SET_PREV_LOG_LINK', getPrevLink(res))
}

async function fetchPrev() {
  const [, res] = await tryAsync(
    http.get(`/maxscale/logs/entries?${store.getters['logs/prevLogsParams']}`)
  )
  store.commit('logs/SET_PREV_LOGS', Object.freeze(getData(res)))
  store.commit('logs/SET_PREV_LOG_LINK', getPrevLink(res))
}

export default { fetchLatest, fetchPrev }
