/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 * Copyright (c) 2023 MariaDB plc, Finnish Branch
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2027-11-30
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import Vuex from 'vuex'
import { APP_CONFIG } from '@rootSrc/utils/constants'
import commonConfig from '@share/config'
import modules from './modules'
import plugins from './plugins'

const store = new Vuex.Store({
    plugins,
    state: {
        app_config: { ...APP_CONFIG, COMMON_CONFIG: commonConfig },
        search_keyword: '',
        update_availability: false,
        prev_route: null,
        module_parameters: [],
        form_type: '',
        should_refresh_resource: false,
    },
    mutations: {
        /**
         * @param {String} keyword global search keyword
         */
        SET_SEARCH_KEYWORD(state, keyword) {
            state.search_keyword = keyword
        },
        SET_UPDATE_AVAILABILITY(state, val) {
            state.update_availability = val
        },
        SET_PREV_ROUTE(state, prev_route) {
            state.prev_route = prev_route
        },
        SET_MODULE_PARAMETERS(state, module_parameters) {
            state.module_parameters = module_parameters
        },
        SET_FORM_TYPE(state, form_type) {
            state.form_type = form_type
        },
        SET_REFRESH_RESOURCE(state, boolean) {
            state.should_refresh_resource = boolean
        },
    },
    actions: {
        /**
         * User will be logged out if maxscale is restarted or maxgui is updated
         * This action checks if an update is available.
         * It should be dispatched on public route when routing occurs
         */
        async checkingForUpdate({ commit }) {
            const res = await this.vue.$http.get(`/`)
            this.vue.$logger.info('Checking for update')
            const resDoc = new DOMParser().parseFromString(res.data, 'text/html')
            const newCommitId = resDoc.getElementsByName('commitId')[0].content
            const currentAppCommitId = document
                .getElementsByName('commitId')[0]
                .getAttribute('content')
            this.vue.$logger.info('MaxGUI commit id:', currentAppCommitId)
            this.vue.$logger.info('MaxGUI new commit id:', newCommitId)
            if (currentAppCommitId !== newCommitId) {
                commit('SET_UPDATE_AVAILABILITY', true)
                this.vue.$logger.info('New version is available')
            }
        },
        /**
         * This function fetch all resources data, if id is not provided,
         * @param {string} [param.id] id of the resource
         * @param {string} param.type type of resource. e.g. servers, services, monitors
         * @param {array} param.fields
         * @return {array|object} Resource data
         */
        async getResourceData(_, { type, id, fields = ['state'] }) {
            const { $helpers, $http, $typy } = this.vue
            let path = `/${type}`
            if (id) path += `/${id}`
            path += `?fields[${type}]=${fields.join(',')}`
            const [, res] = await $helpers.to($http.get(path))
            if (id) return $typy(res, 'data.data').safeObjectOrEmpty
            return $typy(res, 'data.data').safeArray
        },

        async fetchModuleParameters({ commit }, moduleId) {
            try {
                let data = []
                let res = await this.vue.$http.get(
                    `/maxscale/modules/${moduleId}?fields[modules]=parameters`
                )
                if (res.data.data) {
                    const { attributes: { parameters = [] } = {} } = res.data.data
                    data = parameters
                }
                commit('SET_MODULE_PARAMETERS', data)
            } catch (e) {
                this.vue.$logger.error(e)
            }
        },
    },
    modules,
})
export default store
