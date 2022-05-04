/*
 * Copyright (c) 2020 MariaDB Corporation Ab
 *
 * Use of this software is governed by the Business Source License included
 * in the LICENSE.TXT file and at www.mariadb.com/bsl11.
 *
 * Change Date: 2026-04-08
 *
 * On the date above, in accordance with the Business Source License, use
 * of this software will be governed by version 2 or later of the General
 * Public License.
 */
import queryHelper from './queryHelper'
import allMemStatesModules from './allMemStatesModules'
import init, { defWorksheetState } from './initQueryEditorState'
queryHelper.syncStateCreator('editor')

export default {
    namespaced: true,
    state: {
        // Toolbar states
        is_fullscreen: false,
        // worksheet states
        worksheets_arr: init.get_def_worksheets_arr, // persisted
        active_wke_id: '',
    },
    mutations: {
        //Toolbar mutations
        SET_FULLSCREEN(state, payload) {
            state.is_fullscreen = payload
        },
        // worksheet mutations
        ADD_NEW_WKE(state) {
            state.worksheets_arr.push(defWorksheetState())
        },
        DELETE_WKE(state, idx) {
            state.worksheets_arr.splice(idx, 1)
        },
        UPDATE_WKE(state, { idx, wke }) {
            state.worksheets_arr = this.vue.$help.immutableUpdate(state.worksheets_arr, {
                [idx]: { $set: wke },
            })
        },
        SET_ACTIVE_WKE_ID(state, payload) {
            state.active_wke_id = payload
        },
        /**
         * This mutation resets all properties of the provided targetWke object to its initial states
         * except states that stores editor data
         * @param {Object} targetWke - wke to be reset
         */
        REFRESH_WKE(state, targetWke) {
            const idx = state.worksheets_arr.indexOf(targetWke)
            const wke = {
                ...targetWke,
                ...queryHelper.syncStateCreator('queryConn'),
                ...queryHelper.syncStateCreator('queryResult'),
                ...queryHelper.syncStateCreator('schemaSidebar'),
                name: 'WORKSHEET',
            }
            state.worksheets_arr = this.vue.$help.immutableUpdate(state.worksheets_arr, {
                [idx]: { $set: wke },
            })
        },
    },
    actions: {
        chooseActiveWke({ state, commit, dispatch }) {
            const { type = 'blank_wke', id: paramId } = this.router.app.$route.params
            if (paramId) {
                if (type !== 'blank_wke') {
                    /**
                     * Check if there is a worksheet connected to the provided resource id (paramId)
                     * then if it's not the current active worksheet, change current worksheet tab to targetWke.
                     * Otherwise, find an empty worksheet(has not been bound to a connection), set it as active and
                     * dispatch SET_PRE_SELECT_CONN_RSRC to open connection dialog
                     */
                    const targetWke = state.worksheets_arr.find(
                        w => w.active_sql_conn.name === paramId
                    )
                    if (targetWke) commit('SET_ACTIVE_WKE_ID', targetWke.id)
                    else {
                        // Use a blank wke if there is one, otherwise create a new one
                        const blankWke = state.worksheets_arr.find(
                            wke => this.vue.$typy(wke, 'active_sql_conn').isEmptyObject
                        )
                        if (blankWke) commit('SET_ACTIVE_WKE_ID', blankWke.id)
                        else dispatch('addNewWs')
                        commit(
                            'queryConn/SET_PRE_SELECT_CONN_RSRC',
                            { type, id: paramId },
                            { root: true }
                        )
                    }
                }
            } else if (state.worksheets_arr.length) {
                const currActiveWkeId = state.active_wke_id
                const nextActiveWkeId = state.worksheets_arr[0].id
                // set the first wke as active if route param id is not specified
                commit('SET_ACTIVE_WKE_ID', state.worksheets_arr[0].id)
                if (currActiveWkeId === nextActiveWkeId) dispatch('updateRoute', nextActiveWkeId)
            }
        },
        /**
         * This handles updating route for the current active worksheet.
         * If it is bound to a connection, it navigates route to the nested route. i.e /query/:resourceType/:resourceId
         * Otherwise, it uses worksheet id as nested route id. i.e. /query/blank_wke/:wkeId.
         * This function must be called in the following cases:
         * 1. When $route changes. e.g. The use edits url or enter page with an absolute link
         * 2. When active_wke_id is changed. e.g. The user creates new worksheet or navigate between worksheets
         * 3. When active_sql_conn is changed. e.g. The user selects connection in the dropdown or opens new one
         * 4. When the connection is unlinked from the worksheet
         * @param {String} wkeId - worksheet id
         */
        updateRoute({ state }, wkeId) {
            let from = this.router.app.$route.path,
                to = `/query/blank_wke/${wkeId}`
            const targetWke = state.worksheets_arr.find(w => w.id === wkeId)
            const { type, name } = targetWke.active_sql_conn
            if (name) to = `/query/${type}/${name}`
            if (from !== to) this.router.push(to)
        },
        addNewWs({ commit, state, dispatch }) {
            try {
                commit('ADD_NEW_WKE')
                const new_active_wke_id = state.worksheets_arr[state.worksheets_arr.length - 1].id
                commit('SET_ACTIVE_WKE_ID', new_active_wke_id)
                dispatch('querySession/handleAddNewSession', new_active_wke_id, { root: true })
            } catch (e) {
                const logger = this.vue.$logger('store-query-addNewWs')
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
        handleDeleteWke({ state, commit, dispatch }, wkeIdx) {
            const targetWke = state.worksheets_arr[wkeIdx]
            // release memory states of query and queryConn modules
            dispatch('releaseQueryModulesMem', targetWke.id)
            commit('DELETE_WKE', wkeIdx)
            dispatch('querySession/deleteAllSessionsByWkeId', targetWke.id, { root: true })
        },
        /**
         * @param {Object} param.wke - worksheet object to be sync to flat states
         */
        handleSyncWke({ commit }, wke) {
            commit('editor/SYNC_WITH_PERSISTED_OBJ', wke, { root: true })
            commit('queryConn/SYNC_WITH_PERSISTED_OBJ', wke, { root: true })
            commit('queryResult/SYNC_WITH_PERSISTED_OBJ', wke, { root: true })
            commit('schemaSidebar/SYNC_WITH_PERSISTED_OBJ', wke, { root: true })
        },
        /**
         * Release memory for target wke when delete a worksheet or disconnect a
         * connection from a worksheet
         * @param {String} param.wke_id - worksheet id.
         */
        releaseQueryModulesMem({ commit }, wke_id) {
            Object.keys(allMemStatesModules).forEach(namespace => {
                queryHelper.releaseMemory({
                    namespace,
                    commit,
                    id: wke_id,
                    memStates: allMemStatesModules[namespace],
                })
            })
        },
    },
    getters: {
        getActiveWke: state => {
            return state.worksheets_arr.find(wke => wke.id === state.active_wke_id)
        },
    },
}
