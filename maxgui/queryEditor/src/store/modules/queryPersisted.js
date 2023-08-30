/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-08-18
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { addDaysToNow } from '@queryEditorSrc/utils/helpers'

// Place here any query editor states need to be persisted without being cleared when logging out
export default {
    namespaced: true,
    state: {
        query_row_limit: 10000,
        query_confirm_flag: 1,
        query_history: [],
        query_snippets: [],
        query_history_expired_time: addDaysToNow(30),
        query_show_sys_schemas_flag: 1,
        tab_moves_focus: false,
        max_statements: 1000,
    },
    mutations: {
        SET_QUERY_ROW_LIMIT(state, payload) {
            state.query_row_limit = payload
        },
        SET_QUERY_CONFIRM_FLAG(state, payload) {
            state.query_confirm_flag = payload // payload is either 0 or 1
        },
        SET_QUERY_HISTORY(state, payload) {
            state.query_history = payload
        },
        UPDATE_QUERY_HISTORY(state, { idx, payload }) {
            if (idx) state.query_history.splice(idx, 1)
            else state.query_history.unshift(payload)
        },
        SET_QUERY_HISTORY_EXPIRED_TIME(state, timestamp) {
            state.query_history_expired_time = timestamp // Unix time
        },
        UPDATE_QUERY_SNIPPETS(state, { idx, payload }) {
            if (idx) state.query_snippets.splice(idx, 1)
            else state.query_snippets.unshift(payload)
        },
        SET_QUERY_SNIPPETS(state, payload) {
            state.query_snippets = payload
        },
        SET_QUERY_SHOW_SYS_SCHEMAS_FLAG(state, payload) {
            state.query_show_sys_schemas_flag = payload
        },
        SET_TAB_MOVES_FOCUS(state, payload) {
            state.tab_moves_focus = payload
        },
        SET_MAX_STATEMENTS(state, payload) {
            state.max_statements = payload
        },
    },
    actions: {
        /**
         * @param {Number} payload.startTime - time when executing the query
         * @param {String} payload.connection_name - connection_name
         * @param {String} payload.name - name of the query, required when queryType is ACTION_LOGS
         * @param {String} payload.sql - sql
         * @param {Object} payload.res - query response
         * @param {String} payload.queryType - query type in QUERY_LOG_TYPES
         */
        pushQueryLog(
            { commit, rootState },
            { startTime, connection_name, name, sql, res, queryType }
        ) {
            try {
                const maskedQuery = this.vue.$helpers.maskQueryPwd(sql)
                const { capitalizeFirstLetter } = this.vue.$helpers
                const { execution_time, results } = this.vue.$typy(
                    res,
                    'data.data.attributes'
                ).safeObject

                let resultData = {}
                let resSetCount = 0
                let resCount = 0
                for (const res of results) {
                    const { data, message = '', errno } = res
                    const isQueryCanceled =
                        message === rootState.queryEditorConfig.config.QUERY_CANCELED

                    if (isQueryCanceled) {
                        resultData[`INTERRUPT`] = message
                    } else if (data) {
                        ++resSetCount
                        resultData[`Result set ${resSetCount}`] = `${data.length} rows in set.`
                    } else if (this.vue.$typy(errno).isNumber) {
                        let msg = ''
                        Object.keys(res).forEach(
                            key => (msg += `${capitalizeFirstLetter(key)}: ${res[key]}. `)
                        )
                        resultData[`Error`] = msg
                    } else {
                        ++resCount
                        resultData[`Result ${resCount}`] = `${res.affected_rows} rows affected.`
                    }
                }

                let response = ''
                Object.keys(resultData).forEach(key => {
                    response += `${key}: ${resultData[key]} \n`
                })
                let action = {
                    name: maskedQuery, // if no name is defined, use sql as name
                    response,
                    type: queryType,
                }
                // if query is aborted/canceled, there is no execution_time
                if (this.vue.$typy(execution_time).isNumber)
                    action.execution_time = execution_time.toFixed(4)

                if (name) {
                    action.sql = maskedQuery
                    action.name = name
                }
                commit('UPDATE_QUERY_HISTORY', {
                    payload: {
                        date: startTime, // Unix time
                        connection_name,
                        time: this.vue.$helpers.dateFormat({
                            moment: this.vue.$moment,
                            value: startTime,
                            formatType: 'HH:mm:ss',
                        }),
                        action,
                    },
                })
            } catch (e) {
                const logger = this.vue.$logger('store-queryPersisted-pushQueryLog')
                logger.error(e)
                commit(
                    'mxsApp/SET_SNACK_BAR_MESSAGE',
                    {
                        text: [this.vue.$mxs_t('errors.persistentStorage')],
                        type: 'error',
                    },
                    { root: true }
                )
            }
        },
        pushToQuerySnippets({ commit }, { date, name, sql }) {
            try {
                commit('UPDATE_QUERY_SNIPPETS', {
                    payload: {
                        date, // Unix time
                        time: this.vue.$helpers.dateFormat({
                            moment: this.vue.$moment,
                            value: date,
                            formatType: 'HH:mm:ss',
                        }),
                        name,
                        sql: this.vue.$helpers.maskQueryPwd(sql),
                    },
                })
            } catch (e) {
                const logger = this.vue.$logger('store-queryPersisted-pushToQuerySnippets')
                logger.error(e)
                commit(
                    'mxsApp/SET_SNACK_BAR_MESSAGE',
                    {
                        text: [this.vue.$mxs_t('errors.persistentStorage')],
                        type: 'error',
                    },
                    { root: true }
                )
            }
        },
        handleAutoClearQueryHistory({ state, commit }) {
            if (
                this.vue.$helpers.daysDiff({
                    moment: this.vue.$moment,
                    timestamp: state.query_history_expired_time,
                }) <= 0
            ) {
                commit('SET_QUERY_HISTORY', [])
                commit('SET_QUERY_HISTORY_EXPIRED_TIME', addDaysToNow(30))
            }
        },
    },
}
