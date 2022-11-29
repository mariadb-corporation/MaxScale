/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-11-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import queryHelper from '@queryEditorSrc/store/queryHelper'

const statesToBeSynced = queryHelper.syncStateCreator('queryResult')
const memStates = queryHelper.memStateCreator('queryResult')
export default {
    namespaced: true,
    state: {
        ...memStates,
        ...statesToBeSynced,
        is_max_rows_valid: true,
    },
    mutations: {
        ...queryHelper.memStatesMutationCreator(memStates),
        ...queryHelper.syncedStateMutationsCreator({
            statesToBeSynced,
            persistedArrayPath: 'queryTab.query_tabs',
        }),
        SET_IS_MAX_ROWS_VALID(state, payload) {
            state.is_max_rows_valid = payload
        },
    },
    actions: {
        /**
         * @param {String} param.qualified_name - Table id (database_name.table_name).
         * @param {String} param.query_mode - a key in QUERY_MODES. Either PRVW_DATA or PRVW_DATA_DETAILS
         */
        async fetchPrvw(
            { rootState, commit, dispatch, rootGetters },
            { qualified_name, query_mode }
        ) {
            const active_sql_conn = rootState.queryConn.active_sql_conn
            const active_query_tab_id = rootGetters['queryTab/getActiveQueryTabId']
            const request_sent_time = new Date().valueOf()
            try {
                commit(`PATCH_${query_mode}_MAP`, {
                    id: active_query_tab_id,
                    payload: {
                        request_sent_time,
                        total_duration: 0,
                        is_loading: true,
                    },
                })
                let sql, queryName
                const escapedQN = this.vue.$helpers.escapeIdentifiers(qualified_name)
                switch (query_mode) {
                    case rootState.queryEditorConfig.config.QUERY_MODES.PRVW_DATA:
                        sql = `SELECT * FROM ${escapedQN} LIMIT 1000;`
                        queryName = `Preview ${escapedQN} data`
                        break
                    case rootState.queryEditorConfig.config.QUERY_MODES.PRVW_DATA_DETAILS:
                        sql = `DESCRIBE ${escapedQN};`
                        queryName = `View ${escapedQN} details`
                        break
                }

                let res = await this.vue.$queryHttp.post(`/sql/${active_sql_conn.id}/queries`, {
                    sql,
                    max_rows: rootState.queryPersisted.query_row_limit,
                })
                const now = new Date().valueOf()
                const total_duration = ((now - request_sent_time) / 1000).toFixed(4)
                commit(`PATCH_${query_mode}_MAP`, {
                    id: active_query_tab_id,
                    payload: {
                        data: Object.freeze(res.data.data),
                        total_duration: parseFloat(total_duration),
                        is_loading: false,
                    },
                })
                dispatch(
                    'queryPersisted/pushQueryLog',
                    {
                        startTime: now,
                        name: queryName,
                        sql,
                        res,
                        connection_name: active_sql_conn.name,
                        queryType: rootState.queryEditorConfig.config.QUERY_LOG_TYPES.ACTION_LOGS,
                    },
                    { root: true }
                )
            } catch (e) {
                commit(`PATCH_${query_mode}_MAP`, {
                    id: active_query_tab_id,
                    payload: { is_loading: false },
                })
                this.vue.$logger.error(e)
            }
        },
        /**
         * @param {String} query - SQL query string
         */
        async fetchQueryResult({ commit, dispatch, getters, rootState, rootGetters }, query) {
            const active_sql_conn = rootState.queryConn.active_sql_conn
            const request_sent_time = new Date().valueOf()
            const active_query_tab_id = rootGetters['queryTab/getActiveQueryTabId']
            const abort_controller = new AbortController()
            const config = rootState.queryEditorConfig.config
            try {
                commit('PATCH_QUERY_RESULTS_MAP', {
                    id: active_query_tab_id,
                    payload: {
                        data: {},
                        request_sent_time,
                        total_duration: 0,
                        is_loading: true,
                        abort_controller,
                    },
                })

                let res = await this.vue.$queryHttp.post(
                    `/sql/${active_sql_conn.id}/queries`,
                    {
                        sql: query,
                        max_rows: rootState.queryPersisted.query_row_limit,
                    },
                    { signal: abort_controller.signal }
                )
                const now = new Date().valueOf()
                const total_duration = ((now - request_sent_time) / 1000).toFixed(4)
                // If the KILL command was sent for the query is being run, the query request is aborted/canceled
                if (getters.getHasKillFlagMapByQueryTabId(active_query_tab_id)) {
                    commit('PATCH_HAS_KILL_FLAG_MAP', {
                        id: active_query_tab_id,
                        payload: { value: false },
                    })
                    res = {
                        data: {
                            data: {
                                attributes: {
                                    results: [{ message: config.QUERY_CANCELED }],
                                    sql: query,
                                },
                            },
                        },
                    }
                    /**
                     * This is done automatically in queryHttp.interceptors.response. However, because the request
                     * is aborted, this needs to be called manually.
                     */
                    commit(
                        'queryConn/PATCH_IS_CONN_BUSY_MAP',
                        { id: active_query_tab_id, payload: { value: false } },
                        { root: true }
                    )
                } else {
                    const USE_REG = /(use|drop database)\s/i
                    if (query.match(USE_REG))
                        await dispatch('queryConn/updateActiveDb', {}, { root: true })
                }
                commit('PATCH_QUERY_RESULTS_MAP', {
                    id: active_query_tab_id,
                    payload: {
                        data: Object.freeze(res.data.data),
                        total_duration: parseFloat(total_duration),
                        is_loading: false,
                    },
                })
                dispatch(
                    'queryPersisted/pushQueryLog',
                    {
                        startTime: now,
                        sql: query,
                        res,
                        connection_name: active_sql_conn.name,
                        queryType: rootState.queryEditorConfig.config.QUERY_LOG_TYPES.USER_LOGS,
                    },
                    { root: true }
                )
            } catch (e) {
                commit('PATCH_QUERY_RESULTS_MAP', {
                    id: active_query_tab_id,
                    payload: { is_loading: false },
                })
                this.vue.$logger.error(e)
            }
        },
        /**
         * This action uses the current active worksheet connection to send
         * KILL QUERY thread_id
         */
        async stopQuery({ commit, getters, rootGetters, rootState }) {
            const active_sql_conn = rootState.queryConn.active_sql_conn
            const active_query_tab_id = rootGetters['queryTab/getActiveQueryTabId']
            try {
                commit('PATCH_HAS_KILL_FLAG_MAP', {
                    id: active_query_tab_id,
                    payload: { value: true },
                })
                const wkeConn = rootGetters['queryConn/getCurrWkeConn']
                const {
                    data: { data: { attributes: { results = [] } = {} } = {} } = {},
                } = await this.vue.$queryHttp.post(`/sql/${wkeConn.id}/queries`, {
                    sql: `KILL QUERY ${active_sql_conn.attributes.thread_id}`,
                })
                if (this.vue.$typy(results, '[0].errno').isDefined)
                    commit(
                        'mxsApp/SET_SNACK_BAR_MESSAGE',
                        {
                            text: [
                                'Failed to stop the query',
                                ...Object.keys(results[0]).map(key => `${key}: ${results[0][key]}`),
                            ],
                            type: 'error',
                        },
                        { root: true }
                    )
                else {
                    const abort_controller = getters.getAbortControllerByQueryTabId(
                        active_query_tab_id
                    )
                    abort_controller.abort() // abort the running query
                }
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },
        /**
         * This action clears prvw_data and prvw_data_details to empty object.
         * Call this action when user selects option in the sidebar.
         * This ensure sub-tabs in Data Preview tab are generated with fresh data
         */
        clearDataPreview({ commit, rootGetters }) {
            const active_query_tab_id = rootGetters['queryTab/getActiveQueryTabId']
            commit(`PATCH_PRVW_DATA_MAP`, { id: active_query_tab_id })
            commit(`PATCH_PRVW_DATA_DETAILS_MAP`, { id: active_query_tab_id })
        },
    },
    getters: {
        getUserQueryRes: (state, getters, rootState, rootGetters) =>
            state.query_results_map[rootGetters['queryTab/getActiveQueryTabId']] || {},
        getLoadingQueryResultByQueryTabId: state => {
            return query_tab_id => {
                const { is_loading = false } = state.query_results_map[query_tab_id] || {}
                return is_loading
            }
        },
        getAbortControllerByQueryTabId: state => {
            return query_tab_id => {
                const { abort_controller = {} } = state.query_results_map[query_tab_id] || {}
                return abort_controller
            }
        },
        isWkeLoadingQueryResult: (state, getters, rootState, rootGetters) => {
            return wke_id => {
                const queryTabIds = rootGetters['queryTab/getQueryTabsByWkeId'](wke_id).map(
                    s => s.id
                )
                let isLoading = false
                for (const key of Object.keys(state.query_results_map)) {
                    if (queryTabIds.includes(key)) {
                        const { is_loading = false } = state.query_results_map[key] || {}
                        if (is_loading) {
                            isLoading = true
                            break
                        }
                    }
                }
                return isLoading
            }
        },
        getHasKillFlagMapByQueryTabId: state => {
            return query_tab_id => {
                const { value = false } = state.has_kill_flag_map[query_tab_id] || {}
                return value
            }
        },
        getPrvwData: (state, getters, rootState, rootGetters) => mode => {
            let map = state[`${mode.toLowerCase()}_map`]
            if (map) return map[rootGetters['queryTab/getActiveQueryTabId']] || {}
            return {}
        },
        getIsRunBtnDisabledByQueryTabId: (state, getters, rootState, rootGetters) => {
            return id => {
                const queryTab = rootState.queryTab.query_tabs.find(s => s.id === id)
                if (!queryTab) return true
                return (
                    !queryTab.query_txt ||
                    !queryTab.active_sql_conn.id ||
                    rootGetters['queryConn/getIsConnBusyByQueryTabId'](queryTab.id) ||
                    getters.getLoadingQueryResultByQueryTabId(queryTab.id)
                )
            }
        },
        getIsVisBtnDisabledByQueryTabId: (state, getters, rootState, rootGetters) => {
            return id => {
                const queryTab = rootState.queryTab.query_tabs.find(s => s.id === id)
                if (!queryTab) return true
                return (
                    !queryTab.active_sql_conn.id ||
                    (rootGetters['queryConn/getIsConnBusyByQueryTabId'](queryTab.id) &&
                        getters.getLoadingQueryResultByQueryTabId(queryTab.id))
                )
            }
        },
        getChartResultSets: (state, getters, rootState, rootGetters) => ({ scope }) => {
            let resSets = []
            // user query result data
            const userQueryResults = scope.$helpers.stringifyClone(
                scope.$typy(getters.getUserQueryRes, 'data.attributes.results').safeArray
            )
            let resSetCount = 0
            for (const res of userQueryResults) {
                if (res.data) {
                    ++resSetCount
                    resSets.push({ id: `RESULT SET ${resSetCount}`, ...res })
                }
            }
            // preview data
            const { PRVW_DATA, PRVW_DATA_DETAILS } = rootState.queryEditorConfig.config.QUERY_MODES
            const prvwModes = [PRVW_DATA, PRVW_DATA_DETAILS]
            const activePrvwNode = rootGetters['schemaSidebar/getActivePrvwNode']
            for (const mode of prvwModes) {
                const data = scope.$helpers.stringifyClone(
                    scope.$typy(getters.getPrvwData(mode), 'data.attributes.results[0]')
                        .safeObjectOrEmpty
                )
                if (!scope.$typy(data).isEmptyObject) {
                    let resName = ''
                    switch (mode) {
                        case PRVW_DATA:
                            resName = scope.$mxs_t('previewData')
                            break
                        case PRVW_DATA_DETAILS:
                            resName = scope.$mxs_t('viewDetails')
                            break
                    }
                    resSets.push({
                        id: `${resName} of ${activePrvwNode.qualified_name}`,
                        ...data,
                    })
                }
            }
            return resSets
        },
    },
}
