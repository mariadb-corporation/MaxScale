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
import Worksheet from '@wsModels/Worksheet'
import QueryTabTmp from '@wsModels/QueryTabTmp'
import QueryConn from '@wsModels/QueryConn'
import QueryResult from '@wsModels/QueryResult'
import { query } from '@wsSrc/api/query'

export default {
    namespaced: true,
    actions: {
        /**
         * @param {String} param.qualified_name - Table id (database_name.table_name).
         * @param {String} param.query_mode - a key in QUERY_MODES. Either PRVW_DATA or PRVW_DATA_DETAILS
         */
        async fetchPrvw({ rootState, dispatch }, { qualified_name, query_mode }) {
            const activeQueryTabConn = QueryConn.getters('getActiveQueryTabConn')
            const activeQueryTabId = Worksheet.getters('getActiveQueryTabId')
            const request_sent_time = new Date().valueOf()
            let field, sql, queryName
            const escapedQN = this.vue.$helpers.escapeIdentifiers(qualified_name)
            switch (query_mode) {
                case rootState.mxsWorkspace.config.QUERY_MODES.PRVW_DATA:
                    sql = `SELECT * FROM ${escapedQN} LIMIT 1000;`
                    queryName = `Preview ${escapedQN} data`
                    field = 'prvw_data'
                    break
                case rootState.mxsWorkspace.config.QUERY_MODES.PRVW_DATA_DETAILS:
                    sql = `DESCRIBE ${escapedQN};`
                    queryName = `View ${escapedQN} details`
                    field = 'prvw_data_details'
                    break
            }
            QueryTabTmp.update({
                where: activeQueryTabId,
                data(obj) {
                    obj[field].request_sent_time = request_sent_time
                    obj[field].total_duration = 0
                    obj[field].is_loading = true
                },
            })
            const [e, res] = await this.vue.$helpers.to(
                query({
                    id: activeQueryTabConn.id,
                    body: { sql, max_rows: rootState.prefAndStorage.query_row_limit },
                })
            )
            if (e)
                QueryTabTmp.update({
                    where: activeQueryTabId,
                    data(obj) {
                        obj[field].is_loading = false
                    },
                })
            else {
                const now = new Date().valueOf()
                const total_duration = ((now - request_sent_time) / 1000).toFixed(4)
                QueryTabTmp.update({
                    where: activeQueryTabId,
                    data(obj) {
                        obj[field].data = Object.freeze(res.data.data)
                        obj[field].total_duration = parseFloat(total_duration)
                        obj[field].is_loading = false
                    },
                })
                dispatch(
                    'prefAndStorage/pushQueryLog',
                    {
                        startTime: now,
                        name: queryName,
                        sql,
                        res,
                        connection_name: activeQueryTabConn.name,
                        queryType: rootState.mxsWorkspace.config.QUERY_LOG_TYPES.ACTION_LOGS,
                    },
                    { root: true }
                )
            }
        },
        /**
         * @param {String} sql - SQL string
         */
        async fetchUserQuery({ dispatch, getters, rootState }, sql) {
            const activeQueryTabConn = QueryConn.getters('getActiveQueryTabConn')
            const request_sent_time = new Date().valueOf()
            const activeQueryTabId = Worksheet.getters('getActiveQueryTabId')
            const abort_controller = new AbortController()
            const config = rootState.mxsWorkspace.config

            QueryTabTmp.update({
                where: activeQueryTabId,
                data(obj) {
                    obj.query_results.request_sent_time = request_sent_time
                    obj.query_results.total_duration = 0
                    obj.query_results.is_loading = true
                    obj.query_results.abort_controller = abort_controller
                },
            })

            let [e, res] = await this.vue.$helpers.to(
                query({
                    id: activeQueryTabConn.id,
                    body: { sql, max_rows: rootState.prefAndStorage.query_row_limit },
                    config: { signal: abort_controller.signal },
                })
            )

            if (e)
                QueryTabTmp.update({
                    where: activeQueryTabId,
                    data(obj) {
                        obj.query_results.is_loading = false
                    },
                })
            else {
                const now = new Date().valueOf()
                const total_duration = ((now - request_sent_time) / 1000).toFixed(4)
                // If the KILL command was sent for the query is being run, the query request is aborted/canceled
                if (getters.getHasKillFlagMapByQueryTabId(activeQueryTabId)) {
                    QueryTabTmp.update({
                        where: activeQueryTabId,
                        data(obj) {
                            obj.has_kill_flag = false
                        },
                    })
                    QueryConn.update({
                        where: activeQueryTabConn.id,
                        /**
                         * This is done automatically in queryHttp.interceptors.response.
                         * However, because the request is aborted, is_busy needs to be set manually.
                         */
                        data: { is_busy: false },
                    })
                    res = {
                        data: {
                            data: {
                                attributes: {
                                    results: [{ message: config.QUERY_CANCELED }],
                                    sql,
                                },
                            },
                        },
                    }
                } else if (sql.match(/(use|drop database)\s/i))
                    await QueryConn.dispatch('updateActiveDb')

                QueryTabTmp.update({
                    where: activeQueryTabId,
                    data(obj) {
                        obj.query_results.data = Object.freeze(res.data.data)
                        obj.query_results.total_duration = parseFloat(total_duration)
                        obj.query_results.is_loading = false
                    },
                })

                dispatch(
                    'prefAndStorage/pushQueryLog',
                    {
                        startTime: now,
                        sql,
                        res,
                        connection_name: activeQueryTabConn.name,
                        queryType: rootState.mxsWorkspace.config.QUERY_LOG_TYPES.USER_LOGS,
                    },
                    { root: true }
                )
            }
        },
        /**
         * This action uses the current active worksheet connection to send
         * KILL QUERY thread_id
         */
        async stopUserQuery({ commit }) {
            const activeQueryTabConn = QueryConn.getters('getActiveQueryTabConn')
            const activeQueryTabId = Worksheet.getters('getActiveQueryTabId')
            const wkeConn = QueryConn.getters('getActiveWkeConn')
            const [e, res] = await this.vue.$helpers.to(
                query({
                    id: wkeConn.id,
                    body: { sql: `KILL QUERY ${activeQueryTabConn.attributes.thread_id}` },
                })
            )
            if (e) this.vue.$logger.error(e)
            else {
                QueryTabTmp.update({
                    where: activeQueryTabId,
                    data(obj) {
                        obj.has_kill_flag = true
                    },
                })
                const results = this.vue.$typy(res, 'data.data.attributes.results').safeArray
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
                else
                    this.vue
                        .$typy(QueryTabTmp.find(activeQueryTabId), 'abort_controller.abort')
                        .safeFunction() // abort the running query
            }
        },
        /**
         * This action clears prvw_data and prvw_data_details to empty object.
         * Call this action when user selects option in the sidebar.
         * This ensure sub-tabs in Data Preview tab are generated with fresh data
         */
        clearDataPreview() {
            QueryTabTmp.update({
                where: Worksheet.getters('getActiveQueryTabId'),
                data(obj) {
                    obj.prvw_data = {}
                    obj.prvw_data_details = {}
                },
            })
        },
    },
    getters: {
        getQueryResult: () => QueryResult.find(Worksheet.getters('getActiveQueryTabId')) || {},
        getCurrQueryMode: (state, getters) => getters.getQueryResult.curr_query_mode || '',

        // Getters for accessing query data stored in memory
        getQueryTabMem: () => QueryTabTmp.find(Worksheet.getters('getActiveQueryTabId')) || {},
        getUserQueryRes: (state, getters) => getters.getQueryTabMem.query_results || {},
        getPrvwData: (state, getters, rootState) => mode => {
            const { PRVW_DATA, PRVW_DATA_DETAILS } = rootState.mxsWorkspace.config.QUERY_MODES
            switch (mode) {
                case PRVW_DATA:
                    return getters.getQueryTabMem.prvw_data || {}
                case PRVW_DATA_DETAILS:
                    return getters.getQueryTabMem.prvw_data_details || {}
                default:
                    return {}
            }
        },
        // Getters by query_tab_id
        getUserQueryResByQueryTabId: () => query_tab_id => {
            const { query_results = {} } = QueryTabTmp.find(query_tab_id) || {}
            return query_results
        },
        getLoadingQueryResultByQueryTabId: (state, getters) => query_tab_id =>
            getters.getUserQueryResByQueryTabId(query_tab_id).is_loading || false,

        getHasKillFlagMapByQueryTabId: (state, getters) => query_tab_id =>
            getters.getUserQueryResByQueryTabId(query_tab_id).has_kill_flag || false,
    },
}
