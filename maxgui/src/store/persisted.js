/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-10-29
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import { addDaysToNow } from 'utils/helpers'
// Place here any states need to be persisted without being cleared when logging out
export default {
    namespaced: true,
    state: {
        // QUery toolbar's states
        query_max_rows: 10000,
        query_confirm_flag: 1,
        query_history: [],
        query_favorite: [],
        query_history_expired_time: addDaysToNow(30),
        query_show_sys_schemas_flag: 1,
    },
    mutations: {
        SET_QUERY_MAX_ROW(state, payload) {
            state.query_max_rows = payload
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
        UPDATE_QUERY_FAVORITE(state, { idx, payload }) {
            if (idx) state.query_favorite.splice(idx, 1)
            else state.query_favorite.unshift(payload)
        },
        SET_QUERY_FAVORITE(state, payload) {
            state.query_favorite = payload
        },
        SET_QUERY_SHOW_SYS_SCHEMAS_FLAG(state, payload) {
            state.query_show_sys_schemas_flag = payload
        },
    },
    actions: {
        pushQueryLog({ commit }, { startTime, connection_name, query, res }) {
            try {
                commit('UPDATE_QUERY_HISTORY', {
                    payload: {
                        date: startTime, // Unix time
                        connection_name,
                        time: this.vue.$help.dateFormat({
                            value: startTime,
                            formatType: 'HH:mm:ss',
                        }),
                        execution_time: res.data.data.attributes.execution_time.toFixed(4),
                        sql: query,
                    },
                })
            } catch (e) {
                const logger = this.vue.$logger('store-persisted-pushQueryLog')
                logger.error(e)
                commit(
                    'SET_SNACK_BAR_MESSAGE',
                    {
                        text: [this.i18n.t('errors.persistentStorage')],
                        type: 'error',
                    },
                    { root: true }
                )
            }
        },
        pushQueryFavorite({ commit }, { date, name, sql }) {
            try {
                commit('UPDATE_QUERY_FAVORITE', {
                    payload: {
                        date, // Unix time
                        time: this.vue.$help.dateFormat({
                            value: date,
                            formatType: 'HH:mm:ss',
                        }),
                        name,
                        sql,
                    },
                })
            } catch (e) {
                const logger = this.vue.$logger('store-persisted-pushQueryFavorite')
                logger.error(e)
                commit(
                    'SET_SNACK_BAR_MESSAGE',
                    {
                        text: [this.i18n.t('errors.persistentStorage')],
                        type: 'error',
                    },
                    { root: true }
                )
            }
        },
        handleAutoClearQueryHistory({ state, commit }) {
            if (this.vue.$help.daysDiff(state.query_history_expired_time) === 0) {
                commit('SET_QUERY_HISTORY', [])
                commit('SET_QUERY_HISTORY_EXPIRED_TIME', addDaysToNow(30))
            }
        },
    },
}
