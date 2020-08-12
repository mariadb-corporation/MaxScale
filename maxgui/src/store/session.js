/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-07-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    namespaced: true,
    state: {
        all_sessions: [],
        sessions_chart_data: {
            datasets: [],
        },
        sessions_by_service: [],
    },
    mutations: {
        SET_ALL_SESSIONS(state, payload) {
            state.all_sessions = payload
        },
        SET_SESSIONS_CHART_DATA(state, payload) {
            state.sessions_chart_data = payload
        },
        SET_SESSIONS_BY_SERVICE(state, payload) {
            state.sessions_by_service = payload
        },
    },
    actions: {
        async fetchAllSessions({ commit }) {
            try {
                let res = await this.vue.$axios.get(`/sessions`)
                if (res.data.data) commit('SET_ALL_SESSIONS', res.data.data)
            } catch (e) {
                if (process.env.NODE_ENV !== 'test') {
                    const logger = this.vue.$logger('store-sessions-fetchAllSessions')
                    logger.error(e)
                }
            }
        },

        genDataSetSchema({ commit, state }) {
            const { all_sessions } = state
            const { dynamicColors, strReplaceAt } = this.vue.$help
            const lineColor = dynamicColors(0)
            const indexOfOpacity = lineColor.lastIndexOf(')') - 1
            const backgroundColor = strReplaceAt(lineColor, indexOfOpacity, '0.1')

            const dataset = [
                {
                    label: `Total sessions`,
                    type: 'line',
                    // background of the line
                    backgroundColor: backgroundColor,
                    borderColor: lineColor,
                    borderWidth: 1,
                    lineTension: 0,

                    data: [{ x: Date.now(), y: all_sessions.length }],
                },
            ]

            const chartData = {
                datasets: dataset,
            }
            commit('SET_SESSIONS_CHART_DATA', chartData)
        },

        //-------------------- sessions filter by relationships serviceId
        async fetchSessionsFilterByService({ commit }, id) {
            let res = await this.vue.$axios.get(
                `/sessions?filter=/relationships/services/data/0/id="${id}"`
            )
            commit('SET_SESSIONS_BY_SERVICE', res.data.data)
        },
    },
}
