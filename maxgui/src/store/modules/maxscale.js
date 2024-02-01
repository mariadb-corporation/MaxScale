/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
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
import { MXS_OBJ_TYPES } from '@share/constants'
import { TIME_REF_POINTS } from '@src/constants'
import { t } from 'typy'
import { parseDateStr, genSetMutations } from '@share/utils/helpers'

const PAGE_CURSOR_REG = /page\[cursor\]=([^&]+)/
function getPageCursorParam(url) {
    return t(url.match(PAGE_CURSOR_REG), '[0]').safeString
}

const states = () => ({
    all_obj_ids: [],
    maxscale_version: '',
    maxscale_overview_info: {},
    all_modules_map: {},
    thread_stats: [],
    threads_datasets: [],
    maxscale_parameters: {},
    config_sync: null,
    logs_page_size: 100,
    latest_logs: [],
    prev_log_link: null,
    prev_logs: [],
    log_source: null,
    log_filter: {
        session_id: '',
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
        async fetchMaxScaleParameters({ commit }) {
            try {
                let res = await this.vue.$http.get(`/maxscale?fields[maxscale]=parameters`)
                if (res.data.data.attributes.parameters)
                    commit('SET_MAXSCALE_PARAMETERS', res.data.data.attributes.parameters)
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },
        async fetchConfigSync({ commit }) {
            const [, res] = await this.vue.$helpers.tryAsync(
                this.vue.$http.get(`/maxscale?fields[maxscale]=config_sync`)
            )
            commit(
                'SET_CONFIG_SYNC',
                this.vue.$typy(res, 'data.data.attributes.config_sync').safeObject
            )
        },
        async fetchMaxScaleOverviewInfo({ commit }) {
            try {
                let res = await this.vue.$http.get(
                    `/maxscale?fields[maxscale]=version,commit,started_at,activated_at,uptime`
                )
                if (res.data.data.attributes)
                    commit('SET_MAXSCALE_OVERVIEW_INFO', res.data.data.attributes)
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },
        async fetchAllModules({ commit }) {
            const [, res] = await this.vue.$helpers.tryAsync(
                this.vue.$http.get('/maxscale/modules?load=all')
            )
            if (res.data.data) {
                commit(
                    'SET_ALL_MODULES_MAP',
                    this.vue.$helpers.lodash.groupBy(
                        res.data.data,
                        item => item.attributes.module_type
                    )
                )
            }
        },

        async fetchThreadStats({ commit }) {
            try {
                let res = await this.vue.$http.get(`/maxscale/threads?fields[threads]=stats`)
                if (res.data.data) commit('SET_THREAD_STATS', res.data.data)
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },

        genDataSets({ commit, state }) {
            const { thread_stats } = state
            const { genLineStreamDataset } = this.vue.$helpers
            if (thread_stats.length) {
                let dataSets = []
                thread_stats.forEach((thread, i) => {
                    const {
                        attributes: { stats: { load: { last_second = null } = {} } = {} } = {},
                    } = thread
                    if (last_second !== null) {
                        const dataset = genLineStreamDataset({
                            label: `THREAD ID - ${thread.id}`,
                            value: last_second,
                            colorIndex: i,
                        })
                        dataSets.push(dataset)
                    }
                })
                commit('SET_THREADS_DATASETS', dataSets)
            }
        },
        async fetchLatestLogs({ commit, getters }) {
            const [, res] = await this.vue.$helpers.tryAsync(
                this.vue.$http.get(`/maxscale/logs/entries?${getters.logsParams}`)
            )
            const { data = [], links: { prev = '' } = {} } = res.data
            commit('SET_LATEST_LOGS', Object.freeze(data))
            const logSource = this.vue.$typy(data, '[0].attributes.log_source').safeString
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
        //-----------------------------------------------Maxscale parameter update---------------------------------
        /**
         * @param {Object} payload payload object
         * @param {String} payload.id maxscale
         * @param {Object} payload.parameters Parameters for the monitor
         * @param {Object} payload.callback callback function after successfully updated
         */
        async updateMaxScaleParameters({ commit }, payload) {
            try {
                const body = {
                    data: {
                        id: payload.id,
                        type: 'maxscale',
                        attributes: { parameters: payload.parameters },
                    },
                }
                let res = await this.vue.$http.patch(`/maxscale`, body)
                // response ok
                if (res.status === 204) {
                    commit(
                        'mxsApp/SET_SNACK_BAR_MESSAGE',
                        {
                            text: [`MaxScale parameters is updated`],
                            type: 'success',
                        },
                        { root: true }
                    )
                    await this.vue.$typy(payload.callback).safeFunction()
                }
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },
        async fetchAllMxsObjIds({ commit, dispatch }) {
            const types = ['servers', 'monitors', 'filters', 'services', 'listeners']
            let ids = []
            for (const type of types) {
                const data = await dispatch(
                    'getResourceData',
                    { type, fields: ['id'] },
                    { root: true }
                )
                ids.push(...data.map(item => item.id))
            }
            commit('SET_ALL_OBJ_IDS', ids)
        },
    },
    getters: {
        getMxsObjModules: state => objType => {
            const { SERVICES, SERVERS, MONITORS, LISTENERS, FILTERS } = MXS_OBJ_TYPES
            switch (objType) {
                case SERVICES:
                    return t(state.all_modules_map['Router']).safeArray
                case SERVERS:
                    return t(state.all_modules_map['servers']).safeArray
                case MONITORS:
                    return t(state.all_modules_map['Monitor']).safeArray
                case FILTERS:
                    return t(state.all_modules_map['Filter']).safeArray
                case LISTENERS: {
                    let authenticators = t(state.all_modules_map['Authenticator']).safeArray.map(
                        item => item.id
                    )
                    let protocols = t(state.all_modules_map['Protocol']).safeArray || []
                    if (protocols.length) {
                        protocols.forEach(protocol => {
                            protocol.attributes.parameters = protocol.attributes.parameters.filter(
                                o => o.name !== 'protocol' && o.name !== 'service'
                            )
                            // Transform authenticator parameter from string type to enum type,
                            let authenticatorParamObj = protocol.attributes.parameters.find(
                                o => o.name === 'authenticator'
                            )
                            if (authenticatorParamObj) {
                                authenticatorParamObj.type = 'enum'
                                authenticatorParamObj.enum_values = authenticators
                                // add default_value for authenticator
                                authenticatorParamObj.default_value = ''
                            }
                        })
                    }
                    return protocols
                }
                default:
                    return []
            }
        },
        logPriorityParam: state =>
            state.log_filter.priorities.length
                ? `priority=${state.log_filter.priorities.join(',')}`
                : '',
        logDateRangeTimestamp: state =>
            state.log_filter.date_range.map(v => parseDateStr({ v, toTimestamp: true })),
        logDateRangeParam: (state, getters) => {
            const [from, to] = getters.logDateRangeTimestamp
            if (from && to) return `filter=attributes/unix_timestamp=and(ge(${from}),le(${to}))`
            return ''
        },
        logsParams: ({ logs_page_size }, { logPriorityParam, logDateRangeParam }) => {
            let params = [`page[size]=${logs_page_size}`, logDateRangeParam]
            if (logPriorityParam) params.push(logPriorityParam)
            return params.join('&')
        },
        prevPageCursorParam: state => getPageCursorParam(decodeURIComponent(state.prev_log_link)),
        prevLogsParams: (state, getters) => `${getters.prevPageCursorParam}&${getters.logsParams}`,
    },
}
