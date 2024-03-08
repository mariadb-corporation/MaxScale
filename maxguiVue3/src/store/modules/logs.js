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
import { TIME_REF_POINTS } from '@/constants'
import { t as typy } from 'typy'
import { toDateObj, genSetMutations } from '@/utils/helpers'
import { getUnixTime } from 'date-fns'

const PAGE_CURSOR_REG = /page\[cursor\]=([^&]+)/
function getPageCursorParam(url) {
  return typy(url.match(PAGE_CURSOR_REG), '[0]').safeString
}

function genOrExpr(items) {
  return `or(${items.map((item) => `eq("${item}")`).join(',')})`
}

const states = () => ({
  logs_page_size: 100,
  latest_logs: [],
  prev_log_link: null,
  prev_logs: [],
  log_source: null,
  log_filter: {
    session_ids: [],
    obj_ids: [],
    module_ids: [],
    priorities: [],
    date_range: [TIME_REF_POINTS.START_OF_TODAY, TIME_REF_POINTS.NOW],
  },
})

export default {
  namespaced: true,
  state: states(),
  mutations: genSetMutations(states()),
  actions: {
    async fetchLatestLogs({ commit, getters }) {
      const [, res] = await this.vue.$helpers.tryAsync(
        this.vue.$http.get(`/maxscale/logs/entries?${getters.logFilters}`)
      )
      const { data = [], links: { prev = '' } = {} } = res.data
      commit('SET_LATEST_LOGS', Object.freeze(data))
      const logSource = typy(data, '[0].attributes.log_source').safeString
      if (logSource) commit('SET_LOG_SOURCE', logSource)
      commit('SET_PREV_LOG_LINK', prev)
    },
    async fetchPrevLogs({ commit, getters }) {
      const [, res] = await this.vue.$helpers.tryAsync(
        this.vue.$http.get(`/maxscale/logs/entries?${getters.prevLogsParams}`)
      )
      const {
        data,
        links: { prev = '' },
      } = res.data
      commit('SET_PREV_LOGS', Object.freeze(data))
      commit('SET_PREV_LOG_LINK', prev)
    },
  },
  getters: {
    logDateRangeTimestamp: (state) =>
      state.log_filter.date_range.map((v) => getUnixTime(toDateObj(v))),
    logDateRangeFilter: (state, getters) => {
      const [from, to] = getters.logDateRangeTimestamp
      if (from && to) return `filter[$.attributes.unix_timestamp]=and(ge(${from}),le(${to}))`
      return ''
    },
    logPriorityFilter: ({ log_filter: { priorities } }) =>
      priorities.length ? `filter[$.attributes.priority]=${genOrExpr(priorities)}` : '',
    logModuleIdsFilter: ({ log_filter: { module_ids } }) =>
      module_ids.length ? `filter[$.attributes.module]=${genOrExpr(module_ids)}` : '',
    logObjIdsFilter: ({ log_filter: { obj_ids } }) =>
      obj_ids.length ? `filter[$.attributes.object]=${genOrExpr(obj_ids)}` : '',
    logSessionIdFilter: ({ log_filter: { session_ids } }) =>
      session_ids.length ? `filter[$.attributes.session]=${genOrExpr(session_ids)}` : '',
    logFilters: (
      { logs_page_size },
      {
        logDateRangeFilter,
        logPriorityFilter,
        logModuleIdsFilter,
        logObjIdsFilter,
        logSessionIdFilter,
      }
    ) => {
      let params = [`page[size]=${logs_page_size}`, logDateRangeFilter]
      const optionalFilters = [
        logPriorityFilter,
        logModuleIdsFilter,
        logObjIdsFilter,
        logSessionIdFilter,
      ]
      optionalFilters.forEach((filter) => {
        if (filter) params.push(filter)
      })
      return params.join('&')
    },
    prevPageCursorParam: (state) => getPageCursorParam(decodeURIComponent(state.prev_log_link)),
    prevLogsParams: (state, getters) => `${getters.prevPageCursorParam}&${getters.logFilters}`,
  },
}
