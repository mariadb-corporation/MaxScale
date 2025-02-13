/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-04-10
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { MXS_OBJ_TYPES } from '@share/constants'
import { TIME_REF_POINTS } from '@src/constants'
import { t } from 'typy'
import { parseDateStr, genSetMutations } from '@share/utils/helpers'

//TODO: Change link to gitbook
const DOC_BASE_URL = 'https://mariadb.com/kb/en'

const PAGE_CURSOR_REG = /page\[cursor\]=([^&]+)/
const OPTIONAL_FILTER_NAMES = [
    'logPriorityFilter',
    'logModuleIdsFilter',
    'logObjIdsFilter',
    'logSessionIdFilter',
]

function getPageCursorParam(url) {
    return t(url.match(PAGE_CURSOR_REG), '[0]').safeString
}

function genOrExpr(items) {
    return `or(${items.map(item => `eq("${item}")`).join(',')})`
}

function genLogDateRangeFilter(dateRange) {
    const [from, to] = dateRange.map(v => parseDateStr({ v, toTimestamp: true }))
    if (from && to) return `filter[$.attributes.unix_timestamp]=and(ge(${from}),le(${to}))`
    return ''
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
        async fetchVersion({ commit }) {
            const res = await this.vue.$http.get(`/maxscale?fields[maxscale]=version`)
            commit(
                'SET_MAXSCALE_VERSION',
                this.vue.$typy(res, 'data.data.attributes.version').safeString
            )
        },

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
                this.vue.$http.get('/maxscale/logs/entries?' + getters.getLogFilters())
            )
            const { data = [], links: { prev = '' } = {} } = res.data
            commit('SET_LATEST_LOGS', Object.freeze(data))
            const logSource = this.vue.$typy(data, '[0].attributes.log_source').safeString
            if (logSource) commit('SET_LOG_SOURCE', logSource)
            commit('SET_PREV_LOG_LINK', prev)
        },
        async fetchPrevLogs({ commit, getters }) {
            const [, res] = await this.vue.$helpers.tryAsync(
                this.vue.$http.get(
                    '/maxscale/logs/entries?' +
                        `${getters.prevPageCursorParam}&` +
                        getters.getLogFilters()
                )
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
        verData: state => {
            const [major, minor, patch] = state.maxscale_version.split('.')
            return { major, minor, patch }
        },
        ver: (state, getters) => `maxscale-${getters.verData.major}${getters.verData.minor}`,
        docURL: (state, getters) => `${DOC_BASE_URL}/mariadb-${getters.ver}-${getters.ver}`,
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
        logPageSize: ({ logs_page_size }) => `page[size]=${logs_page_size}`,
        logPriorityFilter: ({ log_filter: { priorities } }) =>
            priorities.length ? `filter[$.attributes.priority]=${genOrExpr(priorities)}` : '',
        logModuleIdsFilter: ({ log_filter: { module_ids } }) =>
            module_ids.length ? `filter[$.attributes.module]=${genOrExpr(module_ids)}` : '',
        logObjIdsFilter: ({ log_filter: { obj_ids } }) =>
            obj_ids.length ? `filter[$.attributes.object]=${genOrExpr(obj_ids)}` : '',
        logSessionIdFilter: ({ log_filter: { session_ids } }) =>
            session_ids.length ? `filter[$.attributes.session]=${genOrExpr(session_ids)}` : '',
        optionalLogFilters: (state, getters) =>
            OPTIONAL_FILTER_NAMES.reduce((acc, name) => {
                if (getters[name]) acc.push(getters[name])
                return acc
            }, []),
        /**
         * This getter is implemented as a function to bypass the default caching behavior of Vuex getters,
         * ensuring that the log filters are always recalculated. Normally, Vuex getters are cached based on
         * their dependencies, and they only recompute when those dependencies change. By using a function getter,
         * we force the getter to execute every time it is accessed, ensuring that `genLogDateRangeFilter` is
         * called with the most up-to-date `date_range`. The `date_range` can either be a string representing
         * a date or a TIME_REF_POINT. If the `end` value of the `date_range` is `NOW`, the timestamp will be
         * recalculated to reflect the current time.
         */
        getLogFilters: ({ log_filter: { date_range } }, getters) => () =>
            [
                getters.logPageSize,
                genLogDateRangeFilter(date_range),
                ...getters.optionalLogFilters,
            ].join('&'),
        prevPageCursorParam: state => getPageCursorParam(decodeURIComponent(state.prev_log_link)),
    },
}
