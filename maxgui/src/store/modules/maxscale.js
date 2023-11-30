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
import { APP_CONFIG } from '@rootSrc/utils/constants'
import { t } from 'typy'

export default {
    namespaced: true,
    state: {
        maxscale_version: '',
        maxscale_overview_info: {},
        all_modules_map: {},
        thread_stats: [],
        threads_datasets: [],
        maxscale_parameters: {},
        logs_page_size: 100,
        latest_logs: [],
        prev_log_link: null,
        log_source: null,
        prev_log_data: [],
        prev_filtered_log_link: null,
        prev_filtered_log_data: [],
        hidden_log_levels: [],
    },
    mutations: {
        SET_MAXSCALE_VERSION(state, payload) {
            state.maxscale_version = payload
        },
        SET_MAXSCALE_OVERVIEW_INFO(state, payload) {
            state.maxscale_overview_info = payload
        },
        SET_ALL_MODULES_MAP(state, payload) {
            state.all_modules_map = payload
        },
        SET_THREAD_STATS(state, payload) {
            state.thread_stats = payload
        },
        SET_THREADS_DATASETS(state, payload) {
            state.threads_datasets = payload
        },
        SET_MAXSCALE_PARAMETERS(state, payload) {
            state.maxscale_parameters = payload
        },
        SET_LATEST_LOGS(state, payload) {
            state.latest_logs = payload
        },
        SET_PREV_LOG_LINK(state, payload) {
            state.prev_log_link = payload
        },
        SET_LOG_SOURCE(state, payload) {
            state.log_source = payload
        },
        SET_PREV_LOG_DATA(state, payload) {
            state.prev_log_data = payload
        },
        SET_PREV_FILTERED_LOG_LINK(state, payload) {
            state.prev_filtered_log_link = payload
        },
        SET_PREV_FILTERED_LOG_DATA(state, payload) {
            state.prev_filtered_log_data = payload
        },
        SET_HIDDEN_LOG_LEVELS(state, payload) {
            state.hidden_log_levels = payload
        },
    },
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
            try {
                let res = await this.vue.$http.get(`/maxscale/modules?load=all`)
                if (res.data.data) {
                    const allModules = res.data.data

                    let hashMap = this.vue.$helpers.hashMapByPath({
                        arr: allModules,
                        path: 'attributes.module_type',
                    })

                    commit('SET_ALL_MODULES_MAP', hashMap)
                }
            } catch (e) {
                this.vue.$logger.error(e)
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
        async fetchLatestLogs({ commit, state }) {
            try {
                const res = await this.vue.$http.get(
                    `/maxscale/logs/data?page[size]=${state.logs_page_size}`
                )
                const {
                    data: { attributes: { log = [], log_source = null } = {} } = {},
                    links: { prev = null } = {},
                } = res.data

                if (log.length) commit('SET_LATEST_LOGS', Object.freeze(log))
                if (log_source) commit('SET_LOG_SOURCE', log_source)
                commit('SET_PREV_LOG_LINK', prev)
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },
        /**
         * This function returns previous logData array from previous cursor page link.
         * It also assigns prev link
         * @returns previous logData array
         */
        async fetchPrevLog({ commit, state }) {
            try {
                const indexOfEndpoint = state.prev_log_link.indexOf('/maxscale/logs/')
                const endpoint = state.prev_log_link.slice(indexOfEndpoint)
                const res = await this.vue.$http.get(endpoint)
                const {
                    data: { attributes: { log = [] } = {} } = {},
                    links: { prev = null },
                } = res.data
                commit('SET_PREV_LOG_DATA', log)
                commit('SET_PREV_LOG_LINK', prev)
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },

        async fetchPrevFilteredLog({ commit, state, getters }) {
            try {
                const currPriority = getters.getChosenLogLevels.join(',')
                const prevLink = state.prev_filtered_log_link
                    ? state.prev_filtered_log_link
                    : state.prev_log_link
                const indexOfEndpoint = prevLink.indexOf('/maxscale/logs/')
                const prevEndPoint = prevLink.slice(indexOfEndpoint)
                let endpoint = ''
                if (prevEndPoint.includes('&priority')) {
                    // remove old priority from prevEndPoint
                    let regex = /(alert|debug|error|info|notice|warning),?/g
                    const tmp = prevEndPoint.replace(regex, '')
                    // add current priority
                    endpoint = tmp.replace(/priority=/g, `priority=${currPriority}`)
                } else endpoint = `${prevEndPoint}&priority=${currPriority}`
                const res = await this.vue.$http.get(endpoint)
                const {
                    data: { attributes: { log = [] } = {} } = {},
                    links: { prev = null },
                } = res.data
                commit('SET_PREV_FILTERED_LOG_DATA', log)
                commit('SET_PREV_FILTERED_LOG_LINK', prev)
            } catch (e) {
                this.vue.$logger.error(e)
            }
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
    },
    getters: {
        getMxsObjModules: (state, getters, rootState) => objType => {
            const {
                SERVICES,
                SERVERS,
                MONITORS,
                LISTENERS,
                FILTERS,
            } = rootState.app_config.MXS_OBJ_TYPES
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
        getChosenLogLevels: state =>
            APP_CONFIG.MAXSCALE_LOG_LEVELS.filter(type => !state.hidden_log_levels.includes(type)),
    },
}
