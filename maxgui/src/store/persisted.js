/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-08-17
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

// Place here any states need to be persisted without being cleared when logging out
export default {
    namespaced: true,
    state: {
        // QUery toolbar's states
        query_max_rows: 10000,
        query_confirm_flag: 1,
        query_history: [],
        query_favorite: [],
    },
    mutations: {
        SET_QUERY_MAX_ROW(state, payload) {
            state.query_max_rows = payload
        },
        SET_QUERY_CONFIRM_FLAG(state, payload) {
            state.query_confirm_flag = payload // payload is either 0 or 1
        },
        UPDATE_QUERY_HISTORY(state, { idx, payload }) {
            if (idx) state.query_history.splice(idx, 1)
            else state.query_history.push(payload)
        },
        UPDATE_QUERY_FAVORITE(state, { idx, payload }) {
            if (idx) state.query_favorite.splice(idx, 1)
            else state.query_favorite.push(payload)
        },
    },
    actions: {
        pushQueryLog({ commit }, { startTime, connection_name, query, res }) {
            commit('UPDATE_QUERY_HISTORY', {
                payload: {
                    date: this.vue.$help.dateFormat({
                        value: startTime,
                        formatType: 'ddd, DD MMM YYYY',
                    }),
                    connection_name,
                    time: this.vue.$help.dateFormat({
                        value: startTime,
                        formatType: 'HH:mm:ss',
                    }),
                    execution_time: res.data.data.attributes.execution_time.toFixed(4),
                    sql: query,
                },
            })
        },
        pushQueryFavorite({ commit }, { date, name, sql }) {
            commit('UPDATE_QUERY_FAVORITE', {
                payload: {
                    date,
                    name,
                    sql,
                },
            })
        },
    },
}
