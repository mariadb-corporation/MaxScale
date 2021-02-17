/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2025-02-16
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */

export default {
    namespaced: true,
    state: {
        maxscale_overview_info: {},
        all_modules_map: {},
        thread_stats: [],
        threads_datasets: [],
        maxscale_parameters: {},
    },
    mutations: {
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
    },
    actions: {
        async fetchMaxScaleParameters({ commit }) {
            try {
                let res = await this.vue.$axios.get(`/maxscale?fields[maxscale]=parameters`)
                if (res.data.data.attributes.parameters)
                    commit('SET_MAXSCALE_PARAMETERS', res.data.data.attributes.parameters)
            } catch (e) {
                const logger = this.vue.$logger('store-maxscale-fetchMaxScaleParameters')
                logger.error(e)
            }
        },

        async fetchMaxScaleOverviewInfo({ commit }) {
            try {
                let res = await this.vue.$axios.get(
                    `/maxscale?fields[maxscale]=version,commit,started_at,activated_at,uptime`
                )
                if (res.data.data.attributes)
                    commit('SET_MAXSCALE_OVERVIEW_INFO', res.data.data.attributes)
            } catch (e) {
                const logger = this.vue.$logger('store-maxscale-fetchMaxScaleOverviewInfo')
                logger.error(e)
            }
        },
        async fetchAllModules({ commit }) {
            try {
                let res = await this.vue.$axios.get(`/maxscale/modules?load=all`)
                if (res.data.data) {
                    const allModules = res.data.data

                    let hashMap = this.vue.$help.hashMapByPath({
                        arr: allModules,
                        path: 'attributes.module_type',
                    })

                    commit('SET_ALL_MODULES_MAP', hashMap)
                }
            } catch (e) {
                const logger = this.vue.$logger('store-maxscale-fetchAllModules')
                logger.error(e)
            }
        },

        async fetchThreadStats({ commit }) {
            try {
                let res = await this.vue.$axios.get(`/maxscale/threads?fields[threads]=stats`)
                if (res.data.data) commit('SET_THREAD_STATS', res.data.data)
            } catch (e) {
                const logger = this.vue.$logger('store-maxscale-fetchThreadStats')
                logger.error(e)
            }
        },

        genDataSets({ commit, state }) {
            const { thread_stats } = state
            const { genLineDataSet } = this.vue.$help
            if (thread_stats.length) {
                let dataSets = []
                thread_stats.forEach((thread, i) => {
                    const {
                        attributes: { stats: { load: { last_second = null } = {} } = {} } = {},
                    } = thread
                    if (last_second !== null) {
                        const dataset = genLineDataSet({
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
                let res = await this.vue.$axios.patch(`/maxscale`, body)
                // response ok
                if (res.status === 204) {
                    commit(
                        'SET_SNACK_BAR_MESSAGE',
                        {
                            text: [`MaxScale parameters is updated`],
                            type: 'success',
                        },
                        { root: true }
                    )
                    if (this.vue.$help.isFunction(payload.callback)) await payload.callback()
                }
            } catch (e) {
                const logger = this.vue.$logger('store-maxscale-updateMaxScaleParameters')
                logger.error(e)
            }
        },
    },
}
