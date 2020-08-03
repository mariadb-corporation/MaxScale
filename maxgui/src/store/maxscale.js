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
        maxScaleOverviewInfo: {},

        allModulesMap: {},
        threads: [],
        threadsChartData: {
            datasets: [],
        },
        maxScaleParameters: {},
    },
    mutations: {
        setMaxScaleOverviewInfo(state, payload) {
            state.maxScaleOverviewInfo = payload
        },
        setAllModulesMap(state, payload) {
            state.allModulesMap = payload
        },
        // ---------------------------- last two second threads--------------------------
        setThreads(state, payload) {
            state.threads = payload
        },
        setThreadsChartData(state, payload) {
            state.threadsChartData = payload
        },
        setMaxScaleParameters(state, payload) {
            state.maxScaleParameters = payload
        },
    },
    actions: {
        async fetchMaxScaleParameters({ commit }) {
            try {
                let res = await this.Vue.axios.get(`/maxscale?fields[maxscale]=parameters`)
                if (res.data.data.attributes.parameters)
                    commit('setMaxScaleParameters', res.data.data.attributes.parameters)
            } catch (e) {
                if (process.env.NODE_ENV !== 'test') {
                    const logger = this.Vue.Logger('store-maxscale-fetchMaxScaleParameters')
                    logger.error(e)
                }
            }
        },

        async fetchMaxScaleOverviewInfo({ commit }) {
            try {
                let res = await this.Vue.axios.get(
                    `/maxscale?fields[maxscale]=version,commit,started_at,activated_at,uptime`
                )
                if (res.data.data.attributes)
                    commit('setMaxScaleOverviewInfo', res.data.data.attributes)
            } catch (e) {
                if (process.env.NODE_ENV !== 'test') {
                    const logger = this.Vue.Logger('store-maxscale-fetchMaxScaleOverviewInfo')
                    logger.error(e)
                }
            }
        },
        async fetchAllModules({ commit }) {
            try {
                let res = await this.Vue.axios.get(`/maxscale/modules?load=all`)
                if (res.data.data) {
                    const allModules = res.data.data
                    let hashArr = {} // O(n log n)
                    for (let i = 0; i < allModules.length; ++i) {
                        const module = allModules[i]
                        const moduleType = allModules[i].attributes.module_type
                        if (hashArr[moduleType] == undefined) hashArr[moduleType] = []
                        hashArr[moduleType].push(module)
                    }
                    commit('setAllModulesMap', hashArr)
                }
            } catch (e) {
                if (process.env.NODE_ENV !== 'test') {
                    const logger = this.Vue.Logger('store-maxscale-fetchAllModules')
                    logger.error(e)
                }
            }
        },
        // ---------------------------- last two second threads--------------------------
        async fetchThreads({ commit }) {
            try {
                let res = await this.Vue.axios.get(`/maxscale/threads?fields[threads]=stats`)
                if (res.data.data) commit('setThreads', res.data.data)
            } catch (e) {
                if (process.env.NODE_ENV !== 'test') {
                    const logger = this.Vue.Logger('store-maxscale-fetchThreads')
                    logger.error(e)
                }
            }
        },

        genDataSetSchema({ commit, state }) {
            const { threads } = state
            if (threads) {
                let arr = []
                let lineColors = []
                //threads.length
                for (let i = 0; i < threads.length; ++i) {
                    lineColors.push(this.Vue.prototype.$help.dynamicColors(i))
                    let indexOfOpacity = lineColors[i].lastIndexOf(')') - 1
                    let obj = {
                        label: `THREAD ID - ${threads[i].id}`,
                        id: `THREAD ID - ${threads[i].id}`,
                        type: 'line',
                        // background of the line
                        backgroundColor: this.Vue.prototype.$help.strReplaceAt(
                            lineColors[i],
                            indexOfOpacity,
                            '0.1'
                        ),
                        borderColor: lineColors[i], //theme.palette.primary.main, // line color
                        borderWidth: 1,
                        lineTension: 0,
                        data: [{ x: Date.now(), y: threads[i].attributes.stats.load.last_second }],
                    }
                    arr.push(obj)
                }
                let threadsChartDataSchema = {
                    datasets: arr,
                }
                commit('setThreadsChartData', threadsChartDataSchema)
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
                let res = await this.Vue.axios.patch(`/maxscale`, body)
                // response ok
                if (res.status === 204) {
                    commit(
                        'showMessage',
                        {
                            text: [`MaxScale parameters is updated`],
                            type: 'success',
                        },
                        { root: true }
                    )
                    if (this.Vue.prototype.$help.isFunction(payload.callback))
                        await payload.callback()
                }
            } catch (e) {
                if (process.env.NODE_ENV !== 'test') {
                    const logger = this.Vue.Logger('store-maxscale-updateMaxScaleParameters')
                    logger.error(e)
                }
            }
        },
    },
    getters: {
        maxScaleParameters: state => state.maxScaleParameters,
        maxScaleOverviewInfo: state => state.maxScaleOverviewInfo,
        allModulesMap: state => state.allModulesMap,
        threadsChartData: state => state.threadsChartData,
        threads: state => state.threads,
    },
}
