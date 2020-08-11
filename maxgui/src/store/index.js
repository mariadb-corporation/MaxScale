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
import Vue from 'vue'
import Vuex from 'vuex'
import user from 'store/user'
import maxscale from './maxscale'
import server from './server'
import service from './service'
import monitor from './monitor'
import filter from './filter'
import session from './session'
import listener from './listener'
import { APP_CONFIG } from 'utils/constants'
import router from 'router'
import { refreshAxiosToken } from 'plugins/axios'

const plugins = store => {
    store.router = router
    store.vue = Vue.prototype
}

export default new Vuex.Store({
    plugins: [plugins],
    state: {
        config: APP_CONFIG,
        message: {
            status: false,
            text: '',
            type: 'info',
        },
        searchKeyWord: '',
        overlay: false,
        isUpdateAvailable: false,
        prevRoute: null,
        moduleParameters: [],
        form_type: null,
    },
    mutations: {
        showOverlay(state, type) {
            state.overlay = type
        },
        hideOverlay(state) {
            state.overlay = false
        },
        /**
         * @param {Object} obj Object message
         * @param {Array} obj.text An array of string
         * @param {String} obj.type Type of response
         */
        showMessage(state, obj) {
            const { text, type, status = true } = obj
            state.message.status = status
            state.message.text = text
            state.message.type = type
        },
        /**
         * @param {String} keyword global search keyword
         */
        setSearchKeyWord(state, keyword) {
            state.searchKeyWord = keyword
        },
        setUpdateAvailable(state, val) {
            state.isUpdateAvailable = val
        },
        setPrevRoute(state, prevRoute) {
            state.prevRoute = prevRoute
        },
        setModuleParameters(state, moduleParameters) {
            state.moduleParameters = moduleParameters
        },
        SET_FORM_TYPE(state, form_type) {
            state.form_type = form_type
        },
    },
    actions: {
        async checkingForUpdate({ commit }) {
            refreshAxiosToken()
            const logger = this.vue.$logger('index-store')
            const res = await this.vue.$axios.get(`/`)
            logger.info('Checking for update')
            const resDoc = new DOMParser().parseFromString(res.data, 'text/html')
            const newCommitId = resDoc.getElementsByName('commitId')[0].content
            const currentAppCommitId = document
                .getElementsByName('commitId')[0]
                .getAttribute('content')
            logger.info('MaxGUI commit id:', currentAppCommitId)
            logger.info('MaxGUI new commit id:', newCommitId)
            if (currentAppCommitId !== newCommitId) {
                commit('setUpdateAvailable', true)
                logger.info('New version is available')
            }
        },
        /**
         * This function fetch all resources state, if resourceId is not provided,
         * otherwise it fetch resource state of a resource based on resourceId
         * @param {String} resourceId id of the resource
         * @param {String} resourceType type of resource. e.g. servers, services, monitors
         * @param {String} caller name of the function calling this function, for debugging purpose
         * @return {Array} Resource state data
         */
        async getResourceState(_, { resourceId, resourceType, caller }) {
            try {
                let data = []
                let res
                if (resourceId) {
                    res = await this.vue.$axios.get(
                        `/${resourceType}/${resourceId}?fields[${resourceType}]=state`
                    )
                } else
                    res = await this.vue.$axios.get(
                        `/${resourceType}?fields[${resourceType}]=state`
                    )

                if (res.data.data) data = res.data.data
                return data
            } catch (e) {
                if (process.env.NODE_ENV !== 'test') {
                    const logger = this.vue.$logger(caller)
                    logger.error(e)
                }
            }
        },

        async fetchModuleParameters({ commit }, moduleId) {
            try {
                let data = []
                let res = await this.vue.$axios.get(
                    `/maxscale/modules/${moduleId}?fields[module]=parameters`
                )
                if (res.data.data) {
                    const { attributes: { parameters = [] } = {} } = res.data.data
                    data = parameters
                }
                commit('setModuleParameters', data)
            } catch (e) {
                if (process.env.NODE_ENV !== 'test') {
                    const logger = this.vue.$logger(`fetchModuleParameters-for-${moduleId}`)
                    logger.error(e)
                }
            }
        },
    },
    getters: {
        searchKeyWord: state => state.searchKeyWord,
        overlay: state => state.overlay,
        moduleParameters: state => state.moduleParameters,
        form_type: state => state.form_type,
    },
    modules: {
        filter,
        listener,
        maxscale,
        monitor,
        server,
        service,
        session,
        user,
    },
})
