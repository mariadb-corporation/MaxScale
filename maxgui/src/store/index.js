/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2024-06-15
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

const plugins = store => {
    store.Vue = Vue
    store.router = router
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
    },
    actions: {
        async checkingForUpdate({ commit }) {
            const logger = this.Vue.Logger('index-store')
            const res = await this.Vue.axios.get(`/`)
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
    },
    getters: {
        searchKeyWord: state => state.searchKeyWord,
        overlay: state => state.overlay,
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
