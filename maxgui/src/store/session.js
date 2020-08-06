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
        allSessions: [],
        sessionsChartData: {
            datasets: [],
        },
        sessionsByService: [],
    },
    mutations: {
        /**
         * @param {Array} payload  // Array of allSessions resources
         */
        setSessions(state, payload) {
            state.allSessions = payload
        },
        setSessionsChartData(state, payload) {
            state.sessionsChartData = payload
        },
        setSessionsByService(state, payload) {
            state.sessionsByService = payload
        },
    },
    actions: {
        async fetchAllSessions({ commit }) {
            try {
                let res = await this.vue.$axios.get(`/sessions`)
                if (res.data.data) commit('setSessions', res.data.data)
            } catch (e) {
                if (process.env.NODE_ENV !== 'test') {
                    const logger = this.vue.$logger('store-sessions-fetchAllSessions')
                    logger.error(e)
                }
            }
        },
        // this function should be called after fetchAllSessions has been fetched
        genDataSetSchema({ commit, state }) {
            const { allSessions } = state

            let lineColors = this.vue.$help.dynamicColors(0)

            let indexOfOpacity = lineColors.lastIndexOf(')') - 1
            let dataset = [
                {
                    label: `Total sessions`,
                    type: 'line',
                    // background of the line
                    backgroundColor: this.vue.$help.strReplaceAt(lineColors, indexOfOpacity, '0.1'),
                    borderColor: lineColors, //theme.palette.primary.main, // line color
                    borderWidth: 1,
                    lineTension: 0,

                    data: [{ x: Date.now(), y: allSessions.length }],
                },
            ]

            let sessionsChartDataSchema = {
                datasets: dataset,
            }
            commit('setSessionsChartData', sessionsChartDataSchema)
        },

        //-------------------- sessions filter by relationships serviceId
        async fetchSessionsFilterByServiceId({ commit }, id) {
            let res = await this.vue.$axios.get(
                `/sessions?filter=/relationships/services/data/0/id="${id}"`
            )
            commit('setSessionsByService', res.data.data)
        },
    },
    getters: {
        allSessions: state => state.allSessions,
        sessionsChartData: state => state.sessionsChartData,
        sessionsByService: state => state.sessionsByService,
    },
}
