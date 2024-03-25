/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2028-02-27
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { genSetMutations, addDaysToNow } from '@share/utils/helpers'
import { CMPL_SNIPPET_KIND, QUERY_CANCELED } from '@wsSrc/constants'
import { maskQueryPwd, queryResErrToStr } from '@wsSrc/utils/queryUtils'

const states = () => ({
    sidebar_pct_width: 0,
    is_sidebar_collapsed: false,
    query_pane_pct_height: 60,
    is_fullscreen: false,
    query_row_limit: 10000,
    query_confirm_flag: true,
    query_history_expired_time: addDaysToNow(30), // Unix time
    query_show_sys_schemas_flag: true,
    tab_moves_focus: false,
    max_statements: 1000,
    identifier_auto_completion: true,
    def_conn_obj_type: 'listeners',
    interactive_timeout: 28800,
    wait_timeout: 28800,
    query_history: [],
    query_snippets: [],
})

// Place here any workspace states need to be persisted without being cleared when logging out
export default {
    namespaced: true,
    state: states(),
    mutations: {
        UPDATE_QUERY_HISTORY(state, { idx, payload }) {
            if (idx) state.query_history.splice(idx, 1)
            else state.query_history.unshift(payload)
        },
        UPDATE_QUERY_SNIPPETS(state, { idx, payload }) {
            if (idx) state.query_snippets.splice(idx, 1)
            else state.query_snippets.unshift(payload)
        },
        ...genSetMutations(states()),
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
        pushQueryLog({ commit }, { startTime, connection_name, name, sql, res, queryType }) {
            try {
                const maskedQuery = maskQueryPwd(sql)
                const { execution_time, results } = this.vue.$typy(
                    res,
                    'data.data.attributes'
                ).safeObject

                let resultData = {}
                let resSetCount = 0
                let resCount = 0
                for (const res of results) {
                    const { data, message = '', errno } = res
                    const isQueryCanceled = message === QUERY_CANCELED

                    if (isQueryCanceled) {
                        resultData[`INTERRUPT`] = message
                    } else if (data) {
                        ++resSetCount
                        resultData[`Result set ${resSetCount}`] = `${data.length} rows in set.`
                    } else if (this.vue.$typy(errno).isNumber) {
                        resultData[`Error`] = queryResErrToStr(res)
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
                            value: startTime,
                            formatType: 'HH:mm:ss',
                        }),
                        action,
                    },
                })
            } catch (e) {
                this.vue.$logger.error(e)
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
                            value: date,
                            formatType: 'HH:mm:ss',
                        }),
                        name,
                        sql: maskQueryPwd(sql),
                    },
                })
            } catch (e) {
                this.vue.$logger.error(e)
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
            if (this.vue.$helpers.daysDiff(state.query_history_expired_time) <= 0) {
                commit('SET_QUERY_HISTORY', [])
                commit('SET_QUERY_HISTORY_EXPIRED_TIME', addDaysToNow(30))
            }
        },
    },
    getters: {
        snippetCompletionItems: state =>
            state.query_snippets.map(q => ({
                label: q.name,
                detail: `SNIPPET - ${q.sql}`,
                insertText: q.sql,
                type: CMPL_SNIPPET_KIND,
            })),
    },
}
